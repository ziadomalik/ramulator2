option(EMBED_UI "Embed visualizer frontend into the binary" OFF)

add_library(visualizer STATIC server/visualizer.cpp)
target_include_directories(visualizer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/server)

target_link_libraries(visualizer PUBLIC Crow::Crow)

if(EMBED_UI)
  target_compile_definitions(visualizer PUBLIC EMBED_UI)

  set(NUXT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")
  set(NUXT_DIST_DIR "${NUXT_DIR}/.output/public")

  message(STATUS "EMBED_UI is ON. Building visualizer frontend...")

  execute_process(
    COMMAND npm install
    WORKING_DIRECTORY ${NUXT_DIR}
    RESULT_VARIABLE NPM_INSTALL_RES
  )

  if(NOT NPM_INSTALL_RES EQUAL 0)
    message(FATAL_ERROR "npm install failed in UI directory")
  endif()

  execute_process(
    COMMAND npm run generate
    WORKING_DIRECTORY ${NUXT_DIR}
    RESULT_VARIABLE NUXT_GENERATE_RES
  )

  if(NOT NUXT_GENERATE_RES EQUAL 0)
    message(FATAL_ERROR "npm run generate failed in UI directory")
  endif()

  file(GLOB_RECURSE FRONTEND_FILES "${NUXT_DIST_DIR}/*")
  cmrc_add_resource_library(visualizer_ui_assets ALIAS cmrc::visualizer_ui_assets NAMESPACE visualizer_ui_assets)

  cmrc_add_resources(visualizer_ui_assets WHENCE ${NUXT_DIST_DIR} ${FRONTEND_FILES})

  target_link_libraries(visualizer PRIVATE cmrc::visualizer_ui_assets)
endif()