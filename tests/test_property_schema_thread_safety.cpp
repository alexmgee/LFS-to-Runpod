/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "visualizer/operator/property_schema.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace lfs::vis::op;

class PropertySchemaThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any existing schemas before tests
        for (int i = 0; i < 1000; ++i) {
            propertySchemas().unregisterSchema("test.op." + std::to_string(i));
        }
    }

    void TearDown() override {
        // Clean up after tests
        for (int i = 0; i < 1000; ++i) {
            propertySchemas().unregisterSchema("test.op." + std::to_string(i));
        }
    }
};

TEST_F(PropertySchemaThreadSafetyTest, ConcurrentRegisterAccess) {
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 250;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([t, &success_count]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                int op_id = t * OPS_PER_THREAD + i;
                std::string id = "test.op." + std::to_string(op_id);

                std::vector<PropertySchema> schemas;
                PropertySchema prop;
                prop.name = "value";
                prop.type = PropertyType::FLOAT;
                prop.min = 0.0;
                prop.max = 1.0;
                schemas.push_back(std::move(prop));

                propertySchemas().registerSchema(id, std::move(schemas));
                success_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS * OPS_PER_THREAD);
}

TEST_F(PropertySchemaThreadSafetyTest, ConcurrentReadWrite) {
    // Pre-register some schemas
    for (int i = 0; i < 100; ++i) {
        std::vector<PropertySchema> schemas;
        PropertySchema prop;
        prop.name = "prop_" + std::to_string(i);
        prop.type = PropertyType::FLOAT;
        schemas.push_back(std::move(prop));
        propertySchemas().registerSchema("test.op." + std::to_string(i), std::move(schemas));
    }

    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};
    std::atomic<int> error_count{0};

    // Reader threads
    std::vector<std::thread> readers;
    for (int t = 0; t < 2; ++t) {
        readers.emplace_back([&stop, &read_count, &error_count]() {
            while (!stop.load()) {
                for (int i = 0; i < 100; ++i) {
                    std::string id = "test.op." + std::to_string(i);
                    const auto* schema = propertySchemas().getSchema(id);
                    if (schema) {
                        read_count++;
                    }
                    const auto* prop = propertySchemas().getPropertySchema(id, "prop_" + std::to_string(i));
                    if (prop) {
                        // Access the returned pointer - this should not crash
                        // even under concurrent writes
                        std::string name = prop->name;
                        if (name.empty()) {
                            error_count++;
                        }
                        read_count++;
                    }
                }
            }
        });
    }

    // Writer thread
    std::thread writer([&stop, &write_count]() {
        while (!stop.load()) {
            for (int i = 0; i < 100; ++i) {
                std::vector<PropertySchema> schemas;
                PropertySchema prop;
                prop.name = "prop_" + std::to_string(i);
                prop.type = PropertyType::INT;
                schemas.push_back(std::move(prop));
                propertySchemas().registerSchema("test.op." + std::to_string(i), std::move(schemas));
                write_count++;
            }
        }
    });

    // Run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);

    for (auto& r : readers) {
        r.join();
    }
    writer.join();

    // Just verify we completed without crashing
    EXPECT_GT(read_count.load(), 0) << "Should have completed some reads";
    EXPECT_GT(write_count.load(), 0) << "Should have completed some writes";
    EXPECT_EQ(error_count.load(), 0) << "Should not have any errors";
}

TEST_F(PropertySchemaThreadSafetyTest, ConcurrentUnregister) {
    // Pre-register schemas
    for (int i = 0; i < 100; ++i) {
        std::vector<PropertySchema> schemas;
        PropertySchema prop;
        prop.name = "value";
        prop.type = PropertyType::STRING;
        schemas.push_back(std::move(prop));
        propertySchemas().registerSchema("test.op." + std::to_string(i), std::move(schemas));
    }

    std::vector<std::thread> threads;

    // Unregister from multiple threads
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t]() {
            for (int i = t * 25; i < (t + 1) * 25; ++i) {
                propertySchemas().unregisterSchema("test.op." + std::to_string(i));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify all unregistered
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(propertySchemas().getSchema("test.op." + std::to_string(i)), nullptr);
    }
}

TEST_F(PropertySchemaThreadSafetyTest, GetPropertySchemaReturnsValidPointer) {
    // Register a schema
    std::vector<PropertySchema> schemas;
    PropertySchema prop;
    prop.name = "test_prop";
    prop.type = PropertyType::FLOAT;
    prop.min = 0.0;
    prop.max = 100.0;
    schemas.push_back(std::move(prop));
    propertySchemas().registerSchema("test.single", std::move(schemas));

    // Get the property schema
    const auto* retrieved = propertySchemas().getPropertySchema("test.single", "test_prop");

    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "test_prop");
    EXPECT_EQ(retrieved->type, PropertyType::FLOAT);
    EXPECT_EQ(retrieved->min, 0.0);
    EXPECT_EQ(retrieved->max, 100.0);

    propertySchemas().unregisterSchema("test.single");
}

TEST_F(PropertySchemaThreadSafetyTest, GetPropertySchemaNonexistent) {
    const auto* result = propertySchemas().getPropertySchema("nonexistent.op", "prop");
    EXPECT_EQ(result, nullptr);
}

TEST_F(PropertySchemaThreadSafetyTest, GetPropertySchemaWrongPropName) {
    std::vector<PropertySchema> schemas;
    PropertySchema prop;
    prop.name = "existing_prop";
    prop.type = PropertyType::BOOL;
    schemas.push_back(std::move(prop));
    propertySchemas().registerSchema("test.wrongprop", std::move(schemas));

    const auto* result = propertySchemas().getPropertySchema("test.wrongprop", "nonexistent_prop");
    EXPECT_EQ(result, nullptr);

    propertySchemas().unregisterSchema("test.wrongprop");
}
