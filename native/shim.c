// tinyjs bundle shim: the .app's main executable.
//
// The tjs-compiled single binary can't be codesigned (txiki appends the app
// bundle after the Mach-O, which codesign rejects), so inside a .app we ship
// the STOCK tjs binary (signs cleanly) plus the app as plain data files in
// Resources/, and this tiny shim — which IS signable — as CFBundleExecutable:
//
//   MyApp.app/Contents/
//     MacOS/<name>            <- this shim
//     MacOS/tjs               <- stock runtime
//     MacOS/launcher          <- window process
//     Resources/app/entry.js  <- app code (data, sealed by the bundle signature)
//
// It just execs: <dir>/tjs run <dir>/../Resources/app/entry.js

#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  char exe[PATH_MAX];
  uint32_t size = sizeof(exe);
  if (_NSGetExecutablePath(exe, &size) != 0) {
    fprintf(stderr, "shim: executable path too long\n");
    return 1;
  }
  char real[PATH_MAX];
  if (!realpath(exe, real)) {
    perror("shim: realpath");
    return 1;
  }
  char *dir = dirname(real); // .../Contents/MacOS

  char tjs[PATH_MAX], entry[PATH_MAX];
  snprintf(tjs, sizeof(tjs), "%s/tjs", dir);
  snprintf(entry, sizeof(entry), "%s/../Resources/app/entry.js", dir);

  execl(tjs, tjs, "run", entry, (char *)NULL);
  perror("shim: exec tjs");
  return 1;
}
