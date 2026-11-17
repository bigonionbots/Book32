# library.json extraScript: force-include the compat header into this library's
# sources only, so Config.h's pin macros never reach other libraries.
import os

Import("env")

here = os.path.join(env.subst("$PROJECT_DIR"), "lib", "Book32_Audio", "compat")
env.Append(CCFLAGS=["-include", os.path.join(here, "InkDeckAudioCompat.h")])
