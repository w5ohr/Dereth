// Tools pane: command runner, build-error explainer, benchmark panel, local
// index, folder sync, URL capture.
//
// STATUS: stub. Owned by work package E in docs/FEATURE-PLAN.md.
#include "app.h"
#include "pane_common.h"
#include "ui/widgets.h"

namespace app {

void App::drawToolsPane() {
  paneHeader("Tools", "Command runner, build errors, benchmark, local index", nullptr);
  paneBody("##tools");
  emptyState("Tools pane is being built (package E)");
  paneEnd();
}

}  // namespace app
