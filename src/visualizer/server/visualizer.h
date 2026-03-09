#pragma once
#include <string>

namespace Visualizer {
  // Starts the web server and the broadcast worker on background threads.
  void start_server(int port);
  // Blocks the main thread until the user presses Ctrl+C. 
  void wait_for_termination();
}