#include <gtest/gtest.h>

#include <vector>

#include "dag.hpp"

TEST(DAG, StandaloneJobsAreInitiallyReady) {
    DAG dag;
    dag.add_job("standalone-job");

    EXPECT_FALSE(dag.has_cycle());
    EXPECT_EQ(dag.initial_ready(), std::vector<std::string>{"standalone-job"});
}

TEST(DAG, CyclicGraphIsRejectedBeforeScheduling) {
    DAG dag;
    dag.add_edge("job-A", "job-B");
    dag.add_edge("job-B", "job-A");

    EXPECT_TRUE(dag.has_cycle());
}
