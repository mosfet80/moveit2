/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Ioan Sucan */

#pragma once

#include <moveit/move_group/move_group_capability.hpp>
#include <moveit_msgs/srv/get_state_validity.hpp>

namespace move_group
{
class MoveGroupStateValidationService : public MoveGroupCapability
{
public:
  MoveGroupStateValidationService();

  void initialize() override;

protected:
  bool isStateValid(const planning_scene_monitor::LockedPlanningSceneRO& ls, const moveit::core::RobotState& rs,
                    const std::string& group_name, const moveit_msgs::msg::Constraints& constraints,
                    std::vector<moveit_msgs::msg::ContactInformation>& contacts,
                    std::vector<moveit_msgs::msg::CostSource>& cost_sources,
                    std::vector<moveit_msgs::msg::ConstraintEvalResult>& constraint_result);

private:
  bool computeService(const std::shared_ptr<rmw_request_id_t>& request_header,
                      const std::shared_ptr<moveit_msgs::srv::GetStateValidity::Request>& req,
                      const std::shared_ptr<moveit_msgs::srv::GetStateValidity::Response>& res);

  rclcpp::Service<moveit_msgs::srv::GetStateValidity>::SharedPtr validity_service_;
};
}  // namespace move_group
