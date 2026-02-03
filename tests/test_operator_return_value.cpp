/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtest/gtest.h>

#include "visualizer/operator/operator_result.hpp"

namespace lfs::vis::op {

class OperatorReturnValueTest : public ::testing::Test {};

TEST_F(OperatorReturnValueTest, DefaultConstructor) {
    OperatorReturnValue rv;
    EXPECT_EQ(rv.status, OperatorResult::CANCELLED);
    EXPECT_TRUE(rv.data.empty());
}

TEST_F(OperatorReturnValueTest, FinishedFactory) {
    auto rv = OperatorReturnValue::finished();
    EXPECT_EQ(rv.status, OperatorResult::FINISHED);
    EXPECT_TRUE(rv.data.empty());
    EXPECT_TRUE(rv.is_finished());
    EXPECT_FALSE(rv.is_cancelled());
}

TEST_F(OperatorReturnValueTest, CancelledFactory) {
    auto rv = OperatorReturnValue::cancelled();
    EXPECT_EQ(rv.status, OperatorResult::CANCELLED);
    EXPECT_TRUE(rv.data.empty());
    EXPECT_TRUE(rv.is_cancelled());
    EXPECT_FALSE(rv.is_finished());
}

TEST_F(OperatorReturnValueTest, RunningModalFactory) {
    auto rv = OperatorReturnValue::running_modal();
    EXPECT_EQ(rv.status, OperatorResult::RUNNING_MODAL);
    EXPECT_TRUE(rv.data.empty());
    EXPECT_TRUE(rv.is_running_modal());
    EXPECT_FALSE(rv.is_finished());
}

TEST_F(OperatorReturnValueTest, PassThroughFactory) {
    auto rv = OperatorReturnValue::pass_through();
    EXPECT_EQ(rv.status, OperatorResult::PASS_THROUGH);
    EXPECT_TRUE(rv.data.empty());
    EXPECT_TRUE(rv.is_pass_through());
    EXPECT_FALSE(rv.is_finished());
}

TEST_F(OperatorReturnValueTest, FinishedWithData) {
    std::unordered_map<std::string, std::any> data;
    data["count"] = 42;
    data["name"] = std::string("test");

    auto rv = OperatorReturnValue::finished_with(std::move(data));

    EXPECT_EQ(rv.status, OperatorResult::FINISHED);
    EXPECT_TRUE(rv.is_finished());
    EXPECT_EQ(rv.data.size(), 2);
    EXPECT_EQ(std::any_cast<int>(rv.data.at("count")), 42);
    EXPECT_EQ(std::any_cast<std::string>(rv.data.at("name")), "test");
}

TEST_F(OperatorReturnValueTest, DataWithDifferentTypes) {
    std::unordered_map<std::string, std::any> data;
    data["int_val"] = 123;
    data["float_val"] = 3.14f;
    data["double_val"] = 2.718;
    data["string_val"] = std::string("hello");
    data["bool_val"] = true;

    auto rv = OperatorReturnValue::finished_with(std::move(data));

    EXPECT_EQ(std::any_cast<int>(rv.data.at("int_val")), 123);
    EXPECT_FLOAT_EQ(std::any_cast<float>(rv.data.at("float_val")), 3.14f);
    EXPECT_DOUBLE_EQ(std::any_cast<double>(rv.data.at("double_val")), 2.718);
    EXPECT_EQ(std::any_cast<std::string>(rv.data.at("string_val")), "hello");
    EXPECT_TRUE(std::any_cast<bool>(rv.data.at("bool_val")));
}

TEST_F(OperatorReturnValueTest, AggregateInitialization) {
    OperatorReturnValue rv{OperatorResult::FINISHED, {{"key", 100}}};

    EXPECT_EQ(rv.status, OperatorResult::FINISHED);
    EXPECT_EQ(rv.data.size(), 1);
    EXPECT_EQ(std::any_cast<int>(rv.data.at("key")), 100);
}

TEST_F(OperatorReturnValueTest, StatusPredicatesMutuallyExclusive) {
    auto finished = OperatorReturnValue::finished();
    EXPECT_TRUE(finished.is_finished());
    EXPECT_FALSE(finished.is_cancelled());
    EXPECT_FALSE(finished.is_running_modal());
    EXPECT_FALSE(finished.is_pass_through());

    auto cancelled = OperatorReturnValue::cancelled();
    EXPECT_FALSE(cancelled.is_finished());
    EXPECT_TRUE(cancelled.is_cancelled());
    EXPECT_FALSE(cancelled.is_running_modal());
    EXPECT_FALSE(cancelled.is_pass_through());

    auto modal = OperatorReturnValue::running_modal();
    EXPECT_FALSE(modal.is_finished());
    EXPECT_FALSE(modal.is_cancelled());
    EXPECT_TRUE(modal.is_running_modal());
    EXPECT_FALSE(modal.is_pass_through());

    auto pass = OperatorReturnValue::pass_through();
    EXPECT_FALSE(pass.is_finished());
    EXPECT_FALSE(pass.is_cancelled());
    EXPECT_FALSE(pass.is_running_modal());
    EXPECT_TRUE(pass.is_pass_through());
}

TEST_F(OperatorReturnValueTest, EmptyDataAccess) {
    auto rv = OperatorReturnValue::finished();

    EXPECT_TRUE(rv.data.empty());
    EXPECT_EQ(rv.data.find("nonexistent"), rv.data.end());
}

TEST_F(OperatorReturnValueTest, MoveSemantics) {
    std::unordered_map<std::string, std::any> data;
    data["large_vector"] = std::vector<int>(1000, 42);

    auto rv = OperatorReturnValue::finished_with(std::move(data));

    EXPECT_TRUE(data.empty());
    EXPECT_FALSE(rv.data.empty());
    auto& vec = std::any_cast<std::vector<int>&>(rv.data.at("large_vector"));
    EXPECT_EQ(vec.size(), 1000);
    EXPECT_EQ(vec[0], 42);
}

} // namespace lfs::vis::op
