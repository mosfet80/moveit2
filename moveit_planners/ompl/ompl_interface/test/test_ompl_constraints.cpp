/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2020, KU Leuven
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
 *   * Neither the name of KU Leuven nor the names of its
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

/* Author: Jeroen De Maeyer */

/** This file tests the implementation of constraints inheriting from
 * the ompl::base::Constraint class in the file /detail/ompl_constraint.h/cpp.
 * These are used to create an ompl::base::ConstrainedStateSpace to plan with path constraints.
 *
 *  NOTE q = joint positions
 **/

#include "load_test_robot.hpp"

#include <memory>
#include <string>
#include <iostream>

#include <gtest/gtest.h>
#include <Eigen/Dense>

#include <moveit/robot_model/robot_model.hpp>
#include <moveit/robot_state/robot_state.hpp>
#include <moveit/robot_state/conversions.hpp>
#include <moveit/utils/robot_model_test_utils.hpp>
#include <moveit/ompl_interface/detail/ompl_constraints.hpp>
#include <moveit_msgs/msg/constraints.hpp>
#include <moveit/utils/logger.hpp>

#include <ompl/util/Exception.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <ompl/base/spaces/constraint/ProjectedStateSpace.h>
#include <ompl/base/ConstrainedSpaceInformation.h>

rclcpp::Logger getLogger()
{
  return moveit::getLogger("moveit.planners.ompl.test_constraints");
}

/** \brief Number of times to run a test that uses randomly generated input. **/
constexpr int NUM_RANDOM_TESTS = 10;

/** \brief Select a robot link at (num_dofs - different_link_offset) to test another link than the end-effector. **/
constexpr unsigned int DIFFERENT_LINK_OFFSET = 2;

/** \brief Allowed error when comparing Jacobian matrix error.
 *
 * High tolerance because of high finite difference error.
 * (And it is the L1-norm over the whole matrix difference.)
 **/
constexpr double JAC_ERROR_TOLERANCE = 1e-4;

/** \brief Helper function to create a specific position constraint. **/
moveit_msgs::msg::PositionConstraint createPositionConstraint(std::string& base_link, std::string& ee_link)
{
  shape_msgs::msg::SolidPrimitive box_constraint;
  box_constraint.type = shape_msgs::msg::SolidPrimitive::BOX;
  box_constraint.dimensions = { 0.05, 0.4, 0.05 }; /* use -1 to indicate no constraints. */

  geometry_msgs::msg::Pose box_pose;
  box_pose.position.x = 0.9;
  box_pose.position.y = 0.0;
  box_pose.position.z = 0.2;
  box_pose.orientation.w = 1.0;

  moveit_msgs::msg::PositionConstraint position_constraint;
  position_constraint.header.frame_id = base_link;
  position_constraint.link_name = ee_link;
  position_constraint.constraint_region.primitives.push_back(box_constraint);
  position_constraint.constraint_region.primitive_poses.push_back(box_pose);

  return position_constraint;
}

class TestOMPLConstraints : public ompl_interface_testing::LoadTestRobot, public testing::Test
{
protected:
  TestOMPLConstraints(const std::string& robot_name, const std::string& group_name)
    : LoadTestRobot(robot_name, group_name)
  {
  }

  void SetUp() override
  {
  }

  void TearDown() override
  {
  }

  /** \brief Robot forward kinematics. **/
  const Eigen::Isometry3d fk(const Eigen::VectorXd& q, const std::string& link_name) const
  {
    robot_state_->setJointGroupPositions(joint_model_group_, q);
    return robot_state_->getGlobalLinkTransform(link_name);
  }

  Eigen::VectorXd getRandomState()
  {
    robot_state_->setToRandomPositions(joint_model_group_);
    Eigen::VectorXd joint_positions;
    robot_state_->copyJointGroupPositions(joint_model_group_, joint_positions);
    return joint_positions;
  }

  Eigen::MatrixXd numericalJacobianPosition(const Eigen::VectorXd& q, const std::string& link_name) const
  {
    const double h = 1e-6; /* step size for numerical derivation */

    Eigen::MatrixXd jacobian = Eigen::MatrixXd::Zero(3, num_dofs_);

    // helper matrix for differentiation.
    Eigen::MatrixXd m_helper = h * Eigen::MatrixXd::Identity(num_dofs_, num_dofs_);

    for (std::size_t dim = 0; dim < num_dofs_; ++dim)
    {
      Eigen::Vector3d pos = fk(q, link_name).translation();
      Eigen::Vector3d pos_plus_h = fk(q + m_helper.col(dim), link_name).translation();
      Eigen::Vector3d col = (pos_plus_h - pos) / h;
      jacobian.col(dim) = col;
    }
    return jacobian;
  }

  void setPositionConstraints()
  {
    moveit_msgs::msg::Constraints constraint_msgs;
    constraint_msgs.position_constraints.push_back(createPositionConstraint(base_link_name_, ee_link_name_));

    constraint_ = std::make_shared<ompl_interface::BoxConstraint>(robot_model_, group_name_, num_dofs_);
    constraint_->init(constraint_msgs);
  }

  /** \brief Test position constraints a link that is _not_ the end-effector. **/
  void setPositionConstraintsDifferentLink()
  {
    std::string different_link = joint_model_group_->getLinkModelNames().at(num_dofs_ - DIFFERENT_LINK_OFFSET);

    RCLCPP_DEBUG_STREAM(getLogger(), different_link);

    moveit_msgs::msg::Constraints constraint_msgs;
    constraint_msgs.position_constraints.push_back(createPositionConstraint(base_link_name_, different_link));

    constraint_ = std::make_shared<ompl_interface::BoxConstraint>(robot_model_, group_name_, num_dofs_);
    constraint_->init(constraint_msgs);
  }

  void setEqualityPositionConstraints()
  {
    moveit_msgs::msg::PositionConstraint pos_con_msg = createPositionConstraint(base_link_name_, ee_link_name_);

    // Make the tolerance on the x dimension smaller than the threshold used to recognize equality constraints.
    // (see docstring EqualityPositionConstraint::equality_constraint_threshold).
    pos_con_msg.constraint_region.primitives.at(0).dimensions[0] = 0.0005;

    // The unconstrained dimensions are set to a large (unused) value
    pos_con_msg.constraint_region.primitives.at(0).dimensions[1] = 1.0;
    pos_con_msg.constraint_region.primitives.at(0).dimensions[2] = 1.0;

    moveit_msgs::msg::Constraints constraint_msgs;
    constraint_msgs.position_constraints.push_back(pos_con_msg);

    // Tell the planner to use an Equality constraint model
    constraint_msgs.name = "use_equality_constraints";

    constraint_ = std::make_shared<ompl_interface::EqualityPositionConstraint>(robot_model_, group_name_, num_dofs_);
    constraint_->init(constraint_msgs);
  }

  void testJacobian()
  {
    SCOPED_TRACE("testJacobian");

    double total_error = 999.9;

    for (int i = 0; i < NUM_RANDOM_TESTS; ++i)
    {
      auto q = getRandomState();
      auto jac_exact = constraint_->calcErrorJacobian(q);

      Eigen::MatrixXd jac_approx = numericalJacobianPosition(q, constraint_->getLinkName());

      RCLCPP_DEBUG_STREAM(getLogger(), "Analytical jacobian:");
      RCLCPP_DEBUG_STREAM(getLogger(), jac_exact);
      RCLCPP_DEBUG_STREAM(getLogger(), "Finite difference jacobian:");
      RCLCPP_DEBUG_STREAM(getLogger(), jac_approx);

      total_error = (jac_exact - jac_approx).lpNorm<1>();
      EXPECT_LT(total_error, JAC_ERROR_TOLERANCE);
    }
  }

  void testOMPLProjectedStateSpaceConstruction()
  {
    SCOPED_TRACE("testOMPLProjectedStateSpaceConstruction");

    auto state_space = std::make_shared<ompl::base::RealVectorStateSpace>(num_dofs_);
    ompl::base::RealVectorBounds bounds(num_dofs_);

    // get joint limits from the joint model group
    auto joint_limits = joint_model_group_->getActiveJointModelsBounds();
    EXPECT_EQ(joint_limits.size(), num_dofs_);

    for (std::size_t i = 0; i < num_dofs_; ++i)
    {
      EXPECT_EQ(joint_limits[i]->size(), 1u);
      bounds.setLow(i, joint_limits[i]->at(0).min_position_);
      bounds.setHigh(i, joint_limits[i]->at(0).max_position_);
    }

    state_space->setBounds(bounds);

    auto constrained_state_space = std::make_shared<ompl::base::ProjectedStateSpace>(state_space, constraint_);

    // constrained_state_space->setStateSamplerAllocator()

    auto constrained_state_space_info =
        std::make_shared<ompl::base::ConstrainedSpaceInformation>(constrained_state_space);

    // TODO(jeroendm) There are some issue with the sanity checks.
    // But these issues do not prevent us to use the ConstrainedPlanningStateSpace! :)
    // The jacobian test is expected to fail because of the discontinuous constraint derivative.
    // In addition not all samples returned from the state sampler will be valid.
    // For more details: https://github.com/moveit/moveit/issues/2092#issuecomment-669911722
    try
    {
      constrained_state_space->sanityChecks();
    }
    catch (ompl::Exception& ex)
    {
      RCLCPP_ERROR(getLogger(), "Sanity checks did not pass: %s", ex.what());
    }
  }

  void testEqualityPositionConstraints()
  {
    SCOPED_TRACE("testEqualityPositionConstraints");

    EXPECT_NE(constraint_, nullptr) << "First call setEqualityPositionConstraints before calling this test.";

    // all test below are in the assumption that we have equality constraints on the x-position, dimension 0.

    Eigen::VectorXd joint_values = getDeterministicState();

    Eigen::Vector3d f;
    f << 99, 99, 99;  // fill in known but wrong values that should all be overwritten
    constraint_->function(joint_values, f);

    // f should always be zero for unconstrained dimensions
    EXPECT_DOUBLE_EQ(f[1], 0.0);
    EXPECT_DOUBLE_EQ(f[2], 0.0);

    Eigen::MatrixXd jac(3, num_dofs_);
    jac.setOnes();  // fill in known but wrong values that should all be overwritten
    constraint_->jacobian(joint_values, jac);

    for (std::size_t i = 0; i < num_dofs_; ++i)
    {
      // rows for unconstrained dimensions should always be zero
      EXPECT_DOUBLE_EQ(jac(1, i), 0.0);
      EXPECT_DOUBLE_EQ(jac(2, i), 0.0);
    }
    // the constrained x-dimension has some non-zeros
    // ( we checked this is true for the given joint values!)
    EXPECT_NE(f[0], 0.0);
    EXPECT_NE(jac.row(0).squaredNorm(), 0.0);
  }

protected:
  std::shared_ptr<ompl_interface::BaseConstraint> constraint_;
};

/***************************************************************************
 * Run all tests on the Panda robot
 * ************************************************************************/
class PandaConstraintTest : public TestOMPLConstraints
{
protected:
  PandaConstraintTest() : TestOMPLConstraints("panda", "panda_arm")
  {
  }
};

TEST_F(PandaConstraintTest, InitPositionConstraint)
{
  setPositionConstraints();
  setPositionConstraintsDifferentLink();
}

TEST_F(PandaConstraintTest, PositionConstraintJacobian)
{
  setPositionConstraints();
  testJacobian();

  constraint_.reset();
  setPositionConstraintsDifferentLink();
  testJacobian();
}

TEST_F(PandaConstraintTest, PositionConstraintOMPLCheck)
{
  setPositionConstraints();
  testOMPLProjectedStateSpaceConstruction();
  // testOMPLStateSampler();

  constraint_.reset();
  setPositionConstraintsDifferentLink();
  testOMPLProjectedStateSpaceConstruction();
}

TEST_F(PandaConstraintTest, EqualityPositionConstraints)
{
  setEqualityPositionConstraints();
  testOMPLProjectedStateSpaceConstruction();
  testEqualityPositionConstraints();
}
/***************************************************************************
 * Run all tests on the Fanuc robot
 * ************************************************************************/
class FanucConstraintTest : public TestOMPLConstraints
{
protected:
  FanucConstraintTest() : TestOMPLConstraints("fanuc", "manipulator")
  {
  }
};

TEST_F(FanucConstraintTest, InitPositionConstraint)
{
  setPositionConstraints();
  setPositionConstraintsDifferentLink();
}

TEST_F(FanucConstraintTest, PositionConstraintJacobian)
{
  setPositionConstraints();
  testJacobian();

  constraint_.reset();
  setPositionConstraintsDifferentLink();
  testJacobian();
}

TEST_F(FanucConstraintTest, PositionConstraintOMPLCheck)
{
  setPositionConstraints();
  testOMPLProjectedStateSpaceConstruction();
  // testOMPLStateSampler();

  constraint_.reset();
  setPositionConstraintsDifferentLink();
  testOMPLProjectedStateSpaceConstruction();
}

TEST_F(FanucConstraintTest, EqualityPositionConstraints)
{
  setEqualityPositionConstraints();
  testOMPLProjectedStateSpaceConstruction();
  testEqualityPositionConstraints();
}

/***************************************************************************
 * Run all tests on the PR2's left arm
 * ************************************************************************/
class PR2LeftArmConstraintTest : public TestOMPLConstraints
{
protected:
  PR2LeftArmConstraintTest() : TestOMPLConstraints("pr2", "left_arm")
  {
  }
};

TEST_F(PR2LeftArmConstraintTest, InitPositionConstraint)
{
  setPositionConstraints();
  setPositionConstraintsDifferentLink();
}

TEST_F(PR2LeftArmConstraintTest, PositionConstraintJacobian)
{
  setPositionConstraints();
  testJacobian();

  constraint_.reset();
  setPositionConstraintsDifferentLink();
  testJacobian();
}

TEST_F(PR2LeftArmConstraintTest, PositionConstraintOMPLCheck)
{
  setPositionConstraints();
  testOMPLProjectedStateSpaceConstruction();

  constraint_.reset();
  setPositionConstraintsDifferentLink();
  testOMPLProjectedStateSpaceConstruction();
}

TEST_F(PR2LeftArmConstraintTest, EqualityPositionConstraints)
{
  setEqualityPositionConstraints();
  testOMPLProjectedStateSpaceConstruction();
  testEqualityPositionConstraints();
}

/***************************************************************************
 * MAIN
 * ************************************************************************/
int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
