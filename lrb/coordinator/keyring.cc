// Copyright 2026 The low-ram-browser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// lrb_keyring: the desktop's keyring (Secret Service: GNOME Keyring,
// KWallet), for the coordinator. Saved passwords and autofill details live
// there, encrypted at rest and out of every site's process; the coordinator
// asks through this helper (coordinator.cc, "pw-*" and "af-*").
//
//   lrb_keyring store  <schema> <name>=<value>...   the secret on stdin
//   lrb_keyring get    <schema> <name>=<value>...   prints the secret
//   lrb_keyring list   <schema> <attr> <name>=<value>...
//                      prints <attr> of every matching item, one per line,
//                      percent-encoded
//   lrb_keyring delete <schema> <name>=<value>...
//
// Schemas: "password" (org.lrb.Password: site, origin, username) and
// "autofill" (org.lrb.Autofill: profile). Exit status: 0 done, 1 nothing
// found (or the user didn't unlock the keyring), 2 no keyring (no libsecret
// or no Secret Service), 3 bad arguments. Secrets pass through stdin and
// stdout only, never the command line, which other processes can read.
//
// libsecret is loaded at run time, as Chrome does, so lrb runs without it
// (saving passwords is then off) and the build doesn't link it.

// C glue (argv, GLib lists) with no //base for spans: this file opts out of
// Chromium's unsafe-buffer checks (docs/unsafe_buffers.md).
#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include <dlfcn.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "libsecret/secret.h"

namespace {

constexpr int kDone = 0;
constexpr int kNotFound = 1;
constexpr int kNoKeyring = 2;
constexpr int kBadArguments = 3;

const SecretSchema kPasswordSchema = {
    "org.lrb.Password",
    SECRET_SCHEMA_NONE,
    {
        {"site", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {"origin", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {"username", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
    },
    // Reserved by libsecret.
    0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

const SecretSchema kAutofillSchema = {
    "org.lrb.Autofill",
    SECRET_SCHEMA_NONE,
    {
        {"profile", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
    },
    0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// libsecret's functions, from dlopen.
struct Libsecret {
  decltype(&secret_password_storev_sync) store = nullptr;
  decltype(&secret_password_lookupv_sync) lookup = nullptr;
  decltype(&secret_password_clearv_sync) clear = nullptr;
  decltype(&secret_password_free) free_password = nullptr;
  decltype(&secret_service_search_sync) search = nullptr;
  decltype(&secret_item_get_attributes) attributes = nullptr;

  bool Load() {
    void* lib = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
      return false;
    }
    store = reinterpret_cast<decltype(store)>(
        dlsym(lib, "secret_password_storev_sync"));
    lookup = reinterpret_cast<decltype(lookup)>(
        dlsym(lib, "secret_password_lookupv_sync"));
    clear = reinterpret_cast<decltype(clear)>(
        dlsym(lib, "secret_password_clearv_sync"));
    free_password = reinterpret_cast<decltype(free_password)>(
        dlsym(lib, "secret_password_free"));
    search = reinterpret_cast<decltype(search)>(
        dlsym(lib, "secret_service_search_sync"));
    attributes = reinterpret_cast<decltype(attributes)>(
        dlsym(lib, "secret_item_get_attributes"));
    return store && lookup && clear && free_password && search && attributes;
  }
};

std::string Encode(const char* text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char* c = reinterpret_cast<const unsigned char*>(text);
       *c; ++c) {
    if (g_ascii_isalnum(*c) || strchr("._~-@+", *c)) {
      out += static_cast<char>(*c);
    } else {
      out += '%';
      out += kHex[*c >> 4];
      out += kHex[*c & 15];
    }
  }
  return out;
}

// No keyring: libsecret's D-Bus errors (no session bus, no service).
bool NoService(const GError* error) {
  return error && (error->domain == g_dbus_error_quark() ||
                   error->domain == g_io_error_quark());
}

int Fail(GError* error) {
  const int status = NoService(error) ? kNoKeyring : kNotFound;
  fprintf(stderr, "lrb_keyring: %s\n", error ? error->message : "failed");
  if (error) {
    g_error_free(error);
  }
  return status;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    return kBadArguments;
  }
  const std::string command = argv[1];
  const std::string schema_name = argv[2];
  const SecretSchema* schema = schema_name == "password"   ? &kPasswordSchema
                               : schema_name == "autofill" ? &kAutofillSchema
                                                           : nullptr;
  if (!schema) {
    return kBadArguments;
  }
  int first = 3;
  std::string shown;  // list: the attribute to print
  if (command == "list") {
    if (argc < 4) {
      return kBadArguments;
    }
    shown = argv[3];
    first = 4;
  }
  GHashTable* attributes =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  for (int i = first; i < argc; ++i) {
    const char* eq = strchr(argv[i], '=');
    if (!eq || eq == argv[i]) {
      g_hash_table_unref(attributes);
      return kBadArguments;
    }
    g_hash_table_insert(attributes, g_strndup(argv[i], eq - argv[i]),
                        g_strdup(eq + 1));
  }

  Libsecret secret;
  if (!secret.Load()) {
    fprintf(stderr, "lrb_keyring: no libsecret\n");
    g_hash_table_unref(attributes);
    return kNoKeyring;
  }

  int status = kBadArguments;
  GError* error = nullptr;
  if (command == "store") {
    std::string password;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
      password.append(buffer, n);
    }
    std::string label = "lrb: " + schema_name;
    if (const char* origin =
            static_cast<const char*>(g_hash_table_lookup(attributes, "origin"))) {
      label += std::string(" for ") + origin;
    }
    status = secret.store(schema, attributes, SECRET_COLLECTION_DEFAULT,
                          label.c_str(), password.c_str(), nullptr, &error)
                 ? kDone
                 : Fail(error);
  } else if (command == "get") {
    gchar* password = secret.lookup(schema, attributes, nullptr, &error);
    if (password) {
      fputs(password, stdout);
      secret.free_password(password);
      status = kDone;
    } else {
      status = error ? Fail(error) : kNotFound;
    }
  } else if (command == "delete") {
    status = secret.clear(schema, attributes, nullptr, &error) ? kDone
             : error                                          ? Fail(error)
                                                               : kNotFound;
  } else if (command == "list") {
    GList* items = secret.search(
        nullptr, schema, attributes,
        static_cast<SecretSearchFlags>(SECRET_SEARCH_ALL |
                                       SECRET_SEARCH_UNLOCK),
        nullptr, &error);
    if (error) {
      status = Fail(error);
    } else {
      for (GList* item = items; item; item = item->next) {
        GHashTable* found = secret.attributes(static_cast<SecretItem*>(item->data));
        if (const char* value =
                static_cast<const char*>(g_hash_table_lookup(found, shown.c_str()))) {
          printf("%s\n", Encode(value).c_str());
        }
        g_hash_table_unref(found);
      }
      g_list_free_full(items, g_object_unref);
      status = items ? kDone : kNotFound;
    }
  }
  g_hash_table_unref(attributes);
  fflush(stdout);
  return status;
}
