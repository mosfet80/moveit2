generate_parameter_library(default_request_adapter_parameters
                           res/default_request_adapter_params.yaml)

add_library(
  moveit_default_planning_request_adapter_plugins SHARED
  src/check_for_stacked_constraints.cpp src/check_start_state_bounds.cpp
  src/check_start_state_collision.cpp src/validate_workspace_bounds.cpp
  src/resolve_constraint_frames.cpp)

target_link_libraries(moveit_default_planning_request_adapter_plugins
                      default_request_adapter_parameters)

set_target_properties(moveit_default_planning_request_adapter_plugins
                      PROPERTIES VERSION "${${PROJECT_NAME}_VERSION}")
ament_target_dependencies(moveit_default_planning_request_adapter_plugins
                          moveit_core rclcpp pluginlib)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_gmock REQUIRED)

  ament_add_gmock(test_check_start_state_bounds
                  test/test_check_start_state_bounds.cpp)
  target_link_libraries(test_check_start_state_bounds moveit_planning_pipeline
                        moveit_default_planning_request_adapter_plugins)

endif()
