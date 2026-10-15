#!/usr/bin/env python3
"""gn, rooted at lrb's targets: for Chromium's tools/licenses/licenses.py
(LICENSES_GN_PATH), which runs "gn desc" with the default root, whose
//chrome targets assert features args.gn turns off (see build/build.sh).

  LICENSES_GN_PATH=build/gn_lrb_root.py tools/licenses/licenses.py ...
"""

import os
import sys

gn = os.path.join(os.environ.get("CHROMIUM_WORKDIR", os.path.expanduser("~/chromium")),
                  "src", "buildtools", "linux64", "gn")
args = sys.argv[1:]
# The subcommand first ("desc"), then the root: --root-pattern needs a
# subcommand that loads the build graph.
os.execv(gn, [gn] + args[:1] + ["--root-target=//lrb", "--root-pattern=//lrb:all"] + args[1:])
