#include "visualizer.h"
#include <crow.h>

#include <mutex>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_set>

#ifdef EMBED_UI
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(visualizer_ui_assets);
#endif

namespace Visualizer {
  static crow::SimpleApp app;
  static std::thread server_thread;
  static std::thread broadcast_thread;
  static std::atomic<bool> is_running{false};

  // Thread-safe queue for broadcasting & storing events.
  static std::mutex queue_mutex;
  static std::vector<std::string> event_queue;

  // Track all the connected WebSocket clients.
  static std::mutex clients_mutex;
  static std::unordered_set<crow::websocket::connection*> connected_clients;

  void broadcast_loop() {
    while (is_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      std::vector<std::string> events_to_send;

      {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (event_queue.empty()) continue;
        events_to_send.swap(event_queue); 
      }

      // Create a JSON array of the events.
      // TODO(ziad): Use some sort of library for this.
      std::string payload = "[";
      for (size_t i = 0; i < events_to_send.size(); ++i) {
        payload += events_to_send[i];
        if (i < events_to_send.size() - 1) payload += ",";
      }
      payload += "]";

      // Broadcast to all active connections from the UI.
      std::lock_guard<std::mutex> lock(clients_mutex);
      for (auto* conn : connected_clients) {
        conn->send_text(payload);
      }
    }
  }

  void start_server(int port) {
    if (is_running) return;
    is_running = true;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onopen([&](crow::websocket::connection& conn) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        connected_clients.insert(&conn);
        CROW_LOG_INFO << "UI connected to trace stream!";
      })
      .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t close_code) {
        (void)reason;
        (void)close_code;
        std::lock_guard<std::mutex> lock(clients_mutex);
        connected_clients.erase(&conn);
      });
#ifdef EMBED_UI
    auto serve_embedded_asset = [](std::string path) {
      auto fs = cmrc::visualizer_ui_assets::get_filesystem();
      if (path == "" || path == "/") path = "index.html";

      try {
        auto file = fs.open(path);
        crow::response res(std::string(file.begin(), file.end()));

        if (path.ends_with(".js"))
          res.set_header("Content-Type", "application/javascript");
        else if (path.ends_with(".css"))
          res.set_header("Content-Type", "text/css");
        else if (path.ends_with(".html"))
          res.set_header("Content-Type", "text/html");
        else if (path.ends_with(".svg"))
          res.set_header("Content-Type", "image/svg+xml");
        else if (path.ends_with(".json"))
          res.set_header("Content-Type", "application/json");

        return res;
      } catch (const std::exception& e) {
        // Fallback to index.html
        try {
          auto index = fs.open("index.html");
          return crow::response(std::string(index.begin(), index.end()));
        } catch (...) {
          return crow::response(404);
        }
      }
    };

    CROW_ROUTE(app, "/")([serve_embedded_asset]() {
      return serve_embedded_asset("index.html");
    });

    CROW_ROUTE(app, "/<path>")([serve_embedded_asset](const crow::request& req, std::string path){
      (void)req;
      return serve_embedded_asset(path);
    });
#else
    CROW_ROUTE(app, "/")([](){
      return crow::response(200, "Debug API running. Boot Nuxt on localhost:3000.");
    });
#endif

    server_thread = std::thread([port]() {
      app.port(port).multithreaded().run(); 
    });

    broadcast_thread = std::thread(broadcast_loop);
  }

  void wait_for_termination() {
    if (!is_running) return;

    // Block until the user presses Ctrl+C.
    // NOTE: Crow automatically handles SIGINT/SIGTERM.
    if (server_thread.joinable()) {
      server_thread.join(); 
    }

    is_running = false;
    if (broadcast_thread.joinable()) {
      broadcast_thread.join();
    }
  }
}