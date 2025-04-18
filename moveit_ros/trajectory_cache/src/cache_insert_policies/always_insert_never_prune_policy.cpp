// Copyright 2024 Intrinsic Innovation LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/** @file
 * @brief Implementation of a cache insertion policy that always decides to insert and never decides
 * to prune.
 *
 * @see CacheInsertPolicyInterface<KeyT, ValueT, CacheEntryT>
 *
 * @author methylDragon
 */

#include <memory>
#include <sstream>
#include <string>

#include <warehouse_ros/message_collection.h>
#include <moveit/move_group_interface/move_group_interface.hpp>

#include <moveit_msgs/msg/motion_plan_request.hpp>
#include <moveit_msgs/msg/robot_trajectory.hpp>
#include <moveit_msgs/srv/get_cartesian_path.hpp>

#include <moveit/trajectory_cache/cache_insert_policies/always_insert_never_prune_policy.hpp>
#include <moveit/trajectory_cache/features/constant_features.hpp>
#include <moveit/trajectory_cache/features/get_cartesian_path_request_features.hpp>
#include <moveit/trajectory_cache/features/motion_plan_request_features.hpp>
#include <moveit/trajectory_cache/utils/utils.hpp>

namespace moveit_ros
{
namespace trajectory_cache
{

using ::warehouse_ros::MessageCollection;
using ::warehouse_ros::MessageWithMetadata;
using ::warehouse_ros::Metadata;
using ::warehouse_ros::Query;

using ::moveit::core::MoveItErrorCode;
using ::moveit::planning_interface::MoveGroupInterface;

using ::moveit_msgs::msg::MotionPlanRequest;
using ::moveit_msgs::msg::RobotTrajectory;
using ::moveit_msgs::srv::GetCartesianPath;

using ::moveit_ros::trajectory_cache::FeaturesInterface;

namespace
{

const std::string EXECUTION_TIME = "execution_time_s";
const std::string FRACTION = "fraction";
const std::string PLANNING_TIME = "planning_time_s";

}  // namespace

// =================================================================================================
// AlwaysInsertNeverPrunePolicy.
// =================================================================================================
// moveit_msgs::msg::MotionPlanRequest <=> moveit::planning_interface::MoveGroupInterface::Plan

AlwaysInsertNeverPrunePolicy::AlwaysInsertNeverPrunePolicy() : name_("AlwaysInsertNeverPrunePolicy")
{
  exact_matching_supported_features_ = AlwaysInsertNeverPrunePolicy::getSupportedFeatures(/*start_tolerance=*/0.0,
                                                                                          /*goal_tolerance=*/0.0);
}

std::vector<std::unique_ptr<FeaturesInterface<MotionPlanRequest>>>
AlwaysInsertNeverPrunePolicy::getSupportedFeatures(double start_tolerance, double goal_tolerance)
{
  std::vector<std::unique_ptr<FeaturesInterface<MotionPlanRequest>>> out;
  out.reserve(6);

  // Start.
  out.push_back(std::make_unique<WorkspaceFeatures>());
  out.push_back(std::make_unique<StartStateJointStateFeatures>(start_tolerance));

  // Goal.
  out.push_back(std::make_unique<MaxSpeedAndAccelerationFeatures>());
  out.push_back(std::make_unique<GoalConstraintsFeatures>(goal_tolerance));
  out.push_back(std::make_unique<PathConstraintsFeatures>(goal_tolerance));
  out.push_back(std::make_unique<TrajectoryConstraintsFeatures>(goal_tolerance));

  return out;
}

std::string AlwaysInsertNeverPrunePolicy::getName() const
{
  return name_;
}

MoveItErrorCode AlwaysInsertNeverPrunePolicy::checkCacheInsertInputs(const MoveGroupInterface& move_group,
                                                                     const MessageCollection<RobotTrajectory>& /*coll*/,
                                                                     const MotionPlanRequest& key,
                                                                     const MoveGroupInterface::Plan& value)
{
  std::string frame_id = getWorkspaceFrameId(move_group, key.workspace_parameters);

  // Check key.
  if (frame_id.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Workspace frame ID cannot be empty.");
  }
  if (key.goal_constraints.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN, name_ + ": Skipping insert: No goal.");
  }

  // Check value.
  if (value.trajectory.joint_trajectory.points.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN, name_ + ": Empty joint trajectory points.");
  }
  if (value.trajectory.joint_trajectory.joint_names.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Empty joint trajectory joint names.");
  }
  if (!value.trajectory.multi_dof_joint_trajectory.points.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Multi-DOF trajectory plans are not supported.");
  }
  if (value.trajectory.joint_trajectory.header.frame_id.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Trajectory frame ID cannot be empty.");
  }
  if (frame_id != value.trajectory.joint_trajectory.header.frame_id)
  {
    std::stringstream ss;
    ss << "Skipping insert: Plan request frame `" << frame_id << "` does not match plan frame `"
       << value.trajectory.joint_trajectory.header.frame_id << "`.";
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN, ss.str());
  }

  return MoveItErrorCode::SUCCESS;
}

std::vector<MessageWithMetadata<RobotTrajectory>::ConstPtr> AlwaysInsertNeverPrunePolicy::fetchMatchingEntries(
    const MoveGroupInterface& move_group, const MessageCollection<RobotTrajectory>& coll, const MotionPlanRequest& key,
    const MoveGroupInterface::Plan& /*value*/, double exact_match_precision)
{
  Query::Ptr query = coll.createQuery();
  for (const auto& feature : exact_matching_supported_features_)
  {
    if (MoveItErrorCode ret = feature->appendFeaturesAsExactFetchQuery(*query, key, move_group, exact_match_precision);
        !ret)
    {
      return {};
    }
  }
  return coll.queryList(query, /*metadata_only=*/true);
}

bool AlwaysInsertNeverPrunePolicy::shouldPruneMatchingEntry(
    const MoveGroupInterface& /*move_group*/, const MotionPlanRequest& /*key*/,
    const MoveGroupInterface::Plan& /*value*/, const MessageWithMetadata<RobotTrajectory>::ConstPtr& /*matching_entry*/,
    std::string* reason)
{
  if (reason != nullptr)
  {
    *reason = "Never prune.";
  }
  return false;  // Never prune.
}

bool AlwaysInsertNeverPrunePolicy::shouldInsert(const MoveGroupInterface& /*move_group*/,
                                                const MotionPlanRequest& /*key*/,
                                                const MoveGroupInterface::Plan& /*value*/, std::string* reason)
{
  if (reason != nullptr)
  {
    *reason = "Always insert.";
  }
  return true;  // Always insert.
}

MoveItErrorCode AlwaysInsertNeverPrunePolicy::appendInsertMetadata(Metadata& metadata,
                                                                   const MoveGroupInterface& move_group,
                                                                   const MotionPlanRequest& key,
                                                                   const MoveGroupInterface::Plan& value)
{
  for (const auto& feature : exact_matching_supported_features_)
  {
    if (MoveItErrorCode ret = feature->appendFeaturesAsInsertMetadata(metadata, key, move_group); !ret)
    {
      return ret;
    }
  }

  // Append ValueT metadata.
  metadata.append(EXECUTION_TIME, getExecutionTime(value.trajectory));
  metadata.append(PLANNING_TIME, value.planning_time);

  return MoveItErrorCode::SUCCESS;
}

void AlwaysInsertNeverPrunePolicy::reset()
{
  return;
}

// =================================================================================================
// CartesianAlwaysInsertNeverPrunePolicy.
// =================================================================================================
// moveit_msgs::srv::GetCartesianPath::Request <=> moveit_msgs::srv::GetCartesianPath::Response

CartesianAlwaysInsertNeverPrunePolicy::CartesianAlwaysInsertNeverPrunePolicy()
  : name_("CartesianAlwaysInsertNeverPrunePolicy")
{
  exact_matching_supported_features_ =
      CartesianAlwaysInsertNeverPrunePolicy::getSupportedFeatures(/*start_tolerance=*/0.0,
                                                                  /*goal_tolerance=*/0.0, /*min_fraction=*/0.0);
}

std::vector<std::unique_ptr<FeaturesInterface<GetCartesianPath::Request>>>
CartesianAlwaysInsertNeverPrunePolicy::getSupportedFeatures(double start_tolerance, double goal_tolerance,
                                                            double min_fraction)
{
  std::vector<std::unique_ptr<FeaturesInterface<GetCartesianPath::Request>>> out;
  out.reserve(7);

  // Start.
  out.push_back(std::make_unique<CartesianWorkspaceFeatures>());
  out.push_back(std::make_unique<CartesianStartStateJointStateFeatures>(start_tolerance));

  // Goal.
  out.push_back(std::make_unique<CartesianMaxSpeedAndAccelerationFeatures>());
  out.push_back(std::make_unique<CartesianMaxStepAndJumpThresholdFeatures>());
  out.push_back(std::make_unique<CartesianWaypointsFeatures>(goal_tolerance));
  out.push_back(std::make_unique<CartesianPathConstraintsFeatures>(goal_tolerance));

  // Min fraction.
  out.push_back(std::make_unique<QueryOnlyGTEFeature<double, GetCartesianPath::Request>>(FRACTION, min_fraction));

  return out;
}

std::string CartesianAlwaysInsertNeverPrunePolicy::getName() const
{
  return name_;
}

MoveItErrorCode CartesianAlwaysInsertNeverPrunePolicy::checkCacheInsertInputs(
    const MoveGroupInterface& move_group, const MessageCollection<RobotTrajectory>& /*coll*/,
    const GetCartesianPath::Request& key, const GetCartesianPath::Response& value)
{
  std::string frame_id = getCartesianPathRequestFrameId(move_group, key);

  // Check key.
  if (frame_id.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Workspace frame ID cannot be empty.");
  }
  if (key.waypoints.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN, name_ + ": Skipping insert: No waypoints.");
  }

  // Check value.
  if (value.solution.joint_trajectory.points.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN, name_ + ": Empty joint trajectory points.");
  }
  if (value.solution.joint_trajectory.joint_names.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Empty joint trajectory joint names.");
  }
  if (!value.solution.multi_dof_joint_trajectory.points.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Multi-DOF trajectory plans are not supported.");
  }
  if (value.solution.joint_trajectory.header.frame_id.empty())
  {
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN,
                           name_ + ": Skipping insert: Trajectory frame ID cannot be empty.");
  }
  if (frame_id != value.solution.joint_trajectory.header.frame_id)
  {
    std::stringstream ss;
    ss << "Skipping insert: Plan request frame `" << frame_id << "` does not match plan frame `"
       << value.solution.joint_trajectory.header.frame_id << "`.";
    return MoveItErrorCode(MoveItErrorCode::INVALID_MOTION_PLAN, ss.str());
  }

  return MoveItErrorCode::SUCCESS;
}

std::vector<MessageWithMetadata<RobotTrajectory>::ConstPtr> CartesianAlwaysInsertNeverPrunePolicy::fetchMatchingEntries(
    const MoveGroupInterface& move_group, const MessageCollection<RobotTrajectory>& coll,
    const GetCartesianPath::Request& key, const GetCartesianPath::Response& /*value*/, double exact_match_precision)
{
  Query::Ptr query = coll.createQuery();
  for (const auto& feature : exact_matching_supported_features_)
  {
    if (MoveItErrorCode ret = feature->appendFeaturesAsExactFetchQuery(*query, key, move_group, exact_match_precision);
        !ret)
    {
      return {};
    }
  }
  return coll.queryList(query, /*metadata_only=*/true);
}

bool CartesianAlwaysInsertNeverPrunePolicy::shouldPruneMatchingEntry(
    const MoveGroupInterface& /*move_group*/, const GetCartesianPath::Request& /*key*/,
    const GetCartesianPath::Response& /*value*/,
    const MessageWithMetadata<RobotTrajectory>::ConstPtr& /*matching_entry*/, std::string* reason)
{
  if (reason != nullptr)
  {
    *reason = "Never prune.";
  }
  return false;  // Never prune.
}

bool CartesianAlwaysInsertNeverPrunePolicy::shouldInsert(const MoveGroupInterface& /*move_group*/,
                                                         const GetCartesianPath::Request& /*key*/,
                                                         const GetCartesianPath::Response& /*value*/,
                                                         std::string* reason)
{
  if (reason != nullptr)
  {
    *reason = "Always insert.";
  }
  return true;  // Always insert.
}

MoveItErrorCode CartesianAlwaysInsertNeverPrunePolicy::appendInsertMetadata(Metadata& metadata,
                                                                            const MoveGroupInterface& move_group,
                                                                            const GetCartesianPath::Request& key,
                                                                            const GetCartesianPath::Response& value)
{
  for (const auto& feature : exact_matching_supported_features_)
  {
    if (MoveItErrorCode ret = feature->appendFeaturesAsInsertMetadata(metadata, key, move_group); !ret)
    {
      return ret;
    }
  }

  // Append ValueT metadata.
  metadata.append(EXECUTION_TIME, getExecutionTime(value.solution));
  metadata.append(FRACTION, value.fraction);

  return MoveItErrorCode::SUCCESS;
}

void CartesianAlwaysInsertNeverPrunePolicy::reset()
{
  return;
}

}  // namespace trajectory_cache
}  // namespace moveit_ros
