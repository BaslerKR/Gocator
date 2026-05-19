/*
 * @file            SimpleConnect.cpp
 *                  Connect to Gocator sensor, acquire one profile frame,
 *                  and save it to a CSV file.
 *
 * GoPxLSdk Simple Sample
 * Copyright (C) 2026 by LMI Technologies Inc.
 *
 * Licensed under the MIT License.
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include <kApi/kApiDef.h>
#include <kApi/Data/kArray1.h>
#include <GoApi/GoApiLib.h>
#include <GoPxLSdk/Def.h>
#include <GoPxLSdk/GoDiscoveryClient.h>
#include <GoPxLSdk/GoSystem.h>
#include <GoPxLSdk/GoChannelError.h>
#include <GoPxLSdk/GoRequestError.h>
#include <GoPxLSdk/GoGdpClient.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpStamp.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfileUniform.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfilePointCloud.h>

using std::string;
using namespace GoPxLSdk;

const int DISCOVER_TIMEOUT_MSEC = 3000;
const int REST_COMMAND_TIMEOUT_MSEC = 30000;
const int RECEIVE_DATA_TIMEOUT_MSEC = 20000;

const string GOCATOR_CONTROL_PATH = "/controls/gocator";
const string GOCATOR_ADD_OUTPUT_PATH = "/controls/gocator/outputs/commands/add";
const string GOCATOR_REMOVE_ALL_OUTPUT_PATH = "/controls/gocator/outputs/commands/removeAll";

const string CSV_OUTPUT_PATH = "./profile_output.csv";

struct ProfilePoint
{
    double x;
    double z;
    unsigned char intensity;
    bool valid;
};

bool SaveProfileToCsv(const std::vector<ProfilePoint>& points, const string& filepath)
{
    std::ofstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return false;
    }

    file << "Index,X_mm,Z_mm,Intensity,Valid" << std::endl;

    for (size_t i = 0; i < points.size(); i++)
    {
        file << i << ","
             << points[i].x << ","
             << points[i].z << ","
             << (int)points[i].intensity << ","
             << (points[i].valid ? 1 : 0)
             << std::endl;
    }

    file.close();
    return true;
}

int main(int argc, char** argv)
{
    kAssembly goApiLib = kNULL;
    int status = -1;

    std::cout << "=== Gocator Profile Acquire & Save to CSV ===" << std::endl;

    try
    {
        kStatus constructStatus = GoApiLib_Construct(&goApiLib);
        if (constructStatus != kOK)
        {
            std::cerr << "Failed to construct GoApiLib: " << constructStatus << std::endl;
            return -1;
        }
        std::cout << "GoApiLib initialized successfully" << std::endl;

        // Determine connection target: use command-line IP or discover
        kIpAddress targetAddress;
        k16u targetPort = GO_PXL_SDK_DEFAULT_CONTROL_PORT;

        if (argc >= 2)
        {
            // Use IP address provided as command-line argument
            std::cout << "\n[1] Using specified sensor IP: " << argv[1] << std::endl;
            kIpAddress_Parse(&targetAddress, argv[1]);
            targetPort = GO_PXL_SDK_DEFAULT_CONTROL_PORT;
            std::cout << "  Target: " << argv[1] << ":" << targetPort << std::endl;
        }
        else
        {
            // Step 1: Discover sensors
            std::cout << "\n[1] Discovering sensors..." << std::endl;
            auto discovery = std::make_unique<GoDiscoveryClient>();
            discovery->BlockingDiscover(DISCOVER_TIMEOUT_MSEC, false);
            auto& instances = discovery->InstanceList();

            std::cout << "Found " << instances.size() << " sensor(s) on network" << std::endl;

            if (instances.empty())
            {
                std::cerr << "No sensors found." << std::endl;
                kDestroyRef(&goApiLib);
                return -1;
            }

            for (size_t i = 0; i < instances.size(); i++)
            {
                kChar ipAddress[16];
                const GoInstance& instance = instances.at(i);
                kIpAddress_Format(instance.GetIpAddress(), ipAddress, sizeof(ipAddress));
                std::cout << "  Sensor " << i << ": " << ipAddress
                          << " (Port: " << instance.GetControlPort() << ")" << std::endl;
            }

            const GoInstance& firstInstance = instances.at(0);

            if (firstInstance.GetIsRemote())
            {
                std::cerr << "Sensor is remote-controlled." << std::endl;
                kDestroyRef(&goApiLib);
                return -1;
            }

            targetAddress = firstInstance.GetIpAddress();
            targetPort = firstInstance.GetControlPort();
        }

        // Step 2: Connect
        std::cout << "\n[2] Connecting to sensor..." << std::endl;
        auto system = GoSystem(targetAddress, targetPort);
        system.Connect();

        kChar connectedIp[16];
        kIpAddress_Format(system.Address(), connectedIp, sizeof(connectedIp));
        std::cout << "Connected to: " << connectedIp << ":" << system.ControlPort() << std::endl;

        // Step 3: Stop system
        std::cout << "\n[3] Preparing system..." << std::endl;
        try { system.Stop(); } catch (...) {}
        std::cout << "  System ready" << std::endl;

        // Step 4: Detect engine type and determine profile source
        std::cout << "\n[4] Detecting sensor type..." << std::endl;
        string engineId;
        string profileSourceId;
        string scannerPath;

        try
        {
            GoJson sensorsResp = system.Client().Read("/scan/visibleSensors/").GetResponse().Payload();
            GoJson sensors = sensorsResp.At("/sensors");
            if (sensors.Size() > 0)
            {
                engineId = sensors.At("/0/engineId").Get<string>();
                string model = sensors.At("/0/model").Get<string>();
                string serial = sensors.At("/0/serialNumber").Get<string>();
                std::cout << "  Model: " << model << " (S/N: " << serial << ")" << std::endl;
                std::cout << "  Engine: " << engineId << std::endl;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "  Warning: " << e.what() << std::endl;
            engineId = "LMIConfocalLineProfiler";
            std::cout << "  Using default engine: " << engineId << std::endl;
        }

        scannerPath = "/scan/engines/" + engineId + "/scanners/scanner-0";

        // Determine profile source based on engine type
        if (engineId == "LMIConfocalLineProfiler")
        {
            profileSourceId = "scan:" + engineId + ":scanner-0:topUniformProfileLayer0";
        }
        else
        {
            profileSourceId = "scan:" + engineId + ":scanner-0:topUniformProfile";
        }
        std::cout << "  Profile source: " << profileSourceId << std::endl;

        // Step 5: Configure scan mode to Profile with intensity
        std::cout << "\n[5] Configuring scan mode..." << std::endl;
        try
        {
            auto payload = GoJson(R"({
                "parameters" : {
                    "scanModeSettings" : {
                        "scanMode" : 2,
                        "intensityEnabled" : true,
                        "uniformSpacingEnabled" : true
                    }
                }
            })");
            system.Client().Update(scannerPath, payload).CheckResponse(REST_COMMAND_TIMEOUT_MSEC);
            std::cout << "  Profile mode configured" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "  Warning: " << e.what() << std::endl;
            std::cout << "  Continuing with existing settings..." << std::endl;
        }

        // Step 6: Enable Gocator Protocol
        std::cout << "\n[6] Enabling Gocator Protocol..." << std::endl;
        system.Client().Update(GOCATOR_CONTROL_PATH, GoJson(R"({"enabled": true})")).CheckResponse(REST_COMMAND_TIMEOUT_MSEC);
        std::cout << "  Gocator Protocol enabled" << std::endl;

        // Step 7: Clear existing outputs and add correct profile source
        std::cout << "\n[7] Configuring GDP output..." << std::endl;
        try
        {
            system.Client().Call(GOCATOR_REMOVE_ALL_OUTPUT_PATH, GoJson("{}")).CheckResponse(REST_COMMAND_TIMEOUT_MSEC);
            std::cout << "  Cleared existing outputs" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cout << "  Note: " << e.what() << std::endl;
        }

        try
        {
            auto payload = GoJson(R"({
                "source" : ")" + profileSourceId + R"(",
                "outputId" : 0,
                "autoShift" : true
            })");
            system.Client().Call(GOCATOR_ADD_OUTPUT_PATH, payload).CheckResponse(REST_COMMAND_TIMEOUT_MSEC);
            std::cout << "  Added profile output: " << profileSourceId << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "  Error adding output: " << e.what() << std::endl;
            std::cerr << "  Attempting to continue..." << std::endl;
        }

        // Step 8: Connect GDP client
        std::cout << "\n[8] Connecting GDP client..." << std::endl;
        auto gdpClient = std::make_unique<GoGdpClient>();
        k16u gdpPort = system.GdpPort();
        gdpClient->Connect(system.Address(), gdpPort);
        std::cout << "  GDP client connected (port: " << gdpPort << ")" << std::endl;

        // Step 9: Start acquisition
        std::cout << "\n[9] Starting acquisition..." << std::endl;
        system.Start();
        std::cout << "  System started" << std::endl;

        // Step 10: Receive one frame of data
        std::cout << "\n[10] Receiving profile data..." << std::endl;
        gdpClient->ReceiveDataSync(RECEIVE_DATA_TIMEOUT_MSEC);

        const GoDataSet& receivedDataSet = gdpClient->DataSet();
        std::cout << "  Received " << receivedDataSet.Count() << " message(s)" << std::endl;

        // Step 11: Process profile data
        std::cout << "\n[11] Processing data..." << std::endl;
        std::vector<ProfilePoint> profilePoints;
        bool profileFound = false;
        k64u frameIndex = 0;

        for (size_t msgIndex = 0; msgIndex < receivedDataSet.Count(); msgIndex++)
        {
            const auto& msg = receivedDataSet.GdpMsgAt(msgIndex);
            std::cout << "  Message " << msgIndex << ": type=" << (int)msg.Type()
                      << " source=" << msg.DataSourceId() << std::endl;

            switch (msg.Type())
            {
            case MessageType::STAMP:
            {
                const auto& stampMsg = static_cast<const GoGdpStamp&>(msg);
                frameIndex = stampMsg.FrameIndex();
                std::cout << "    Stamp - Frame: " << frameIndex
                          << ", Timestamp: " << stampMsg.Timestamp() << std::endl;
                break;
            }
            case MessageType::UNIFORM_PROFILE:
            {
                const auto& profileMsg = static_cast<const GoGdpProfileUniform&>(msg);
                unsigned int pointCount = profileMsg.Width();
                unsigned int intensityWidth = profileMsg.IntensityWidth();
                auto intensityArray = profileMsg.Intensities();

                std::cout << "    Uniform Profile:" << std::endl;
                std::cout << "      Points: " << pointCount << std::endl;
                std::cout << "      Resolution X: " << profileMsg.Resolution().x
                          << " mm, Z: " << profileMsg.Resolution().z << " mm" << std::endl;
                std::cout << "      Offset X: " << profileMsg.Offset().x
                          << " mm, Z: " << profileMsg.Offset().z << " mm" << std::endl;

                profilePoints.resize(pointCount);
                unsigned int validCount = 0;

                for (unsigned int i = 0; i < pointCount; i++)
                {
                    k16s range;
                    kArray1_Item(profileMsg.Ranges(), i, &range);

                    profilePoints[i].x = profileMsg.Offset().x + profileMsg.Resolution().x * i;
                    profilePoints[i].intensity = 0;

                    if (range != k16S_NULL)
                    {
                        profilePoints[i].z = profileMsg.Offset().z + profileMsg.Resolution().z * range;
                        profilePoints[i].valid = true;
                        validCount++;
                    }
                    else
                    {
                        profilePoints[i].z = 0.0;
                        profilePoints[i].valid = false;
                    }
                }

                if (intensityArray)
                {
                    for (unsigned int i = 0; i < intensityWidth && i < pointCount; i++)
                    {
                        kArray1_Item(profileMsg.Intensities(), i, &(profilePoints[i].intensity));
                    }
                }

                std::cout << "      Valid points: " << validCount << " / " << pointCount << std::endl;
                profileFound = true;
                break;
            }
            case MessageType::PROFILE_POINT_CLOUD:
            {
                const auto& profileMsg = static_cast<const GoGdpProfilePointCloud&>(msg);
                unsigned int pointCount = profileMsg.Width();
                unsigned int intensityWidth = profileMsg.IntensityWidth();
                auto intensityArray = profileMsg.Intensities();

                std::cout << "    Point Cloud Profile:" << std::endl;
                std::cout << "      Points: " << pointCount << std::endl;
                std::cout << "      Resolution X: " << profileMsg.Resolution().x
                          << " mm, Z: " << profileMsg.Resolution().z << " mm" << std::endl;
                std::cout << "      Offset X: " << profileMsg.Offset().x
                          << " mm, Z: " << profileMsg.Offset().z << " mm" << std::endl;

                profilePoints.resize(pointCount);
                unsigned int validCount = 0;

                for (unsigned int i = 0; i < pointCount; i++)
                {
                    kPoint16s point;
                    kArray1_Item(profileMsg.Ranges(), i, &point);

                    profilePoints[i].intensity = 0;

                    if (point.x != k16S_NULL)
                    {
                        profilePoints[i].x = profileMsg.Offset().x + profileMsg.Resolution().x * point.x;
                        profilePoints[i].z = profileMsg.Offset().z + profileMsg.Resolution().z * point.y;
                        profilePoints[i].valid = true;
                        validCount++;
                    }
                    else
                    {
                        profilePoints[i].x = 0.0;
                        profilePoints[i].z = 0.0;
                        profilePoints[i].valid = false;
                    }
                }

                if (intensityArray)
                {
                    for (unsigned int i = 0; i < intensityWidth && i < pointCount; i++)
                    {
                        kArray1_Item(profileMsg.Intensities(), i, &(profilePoints[i].intensity));
                    }
                }

                std::cout << "      Valid points: " << validCount << " / " << pointCount << std::endl;
                profileFound = true;
                break;
            }
            default:
                break;
            }
        }

        // Step 12: Save to CSV
        std::cout << "\n[12] Saving to CSV..." << std::endl;
        if (profileFound && !profilePoints.empty())
        {
            if (SaveProfileToCsv(profilePoints, CSV_OUTPUT_PATH))
            {
                std::cout << "  Saved " << profilePoints.size() << " points to: " << CSV_OUTPUT_PATH << std::endl;
            }
            else
            {
                std::cerr << "  Failed to save CSV file" << std::endl;
            }
        }
        else
        {
            std::cerr << "  No profile data received" << std::endl;
        }

        // Step 13: Cleanup
        std::cout << "\n[13] Stopping and disconnecting..." << std::endl;
        gdpClient->Close();
        system.Stop();
        system.Disconnect();
        std::cout << "  Done" << std::endl;

        std::cout << "\n=== Profile acquisition completed ===" << std::endl;
        status = 0;
    }
    catch (const GoRequestError& e)
    {
        std::cerr << "\nGoRequestError: " << e.what() << std::endl;
        std::cerr << "Path: " << e.GetResponse().Path() << std::endl;
        status = -1;
    }
    catch (const GoChannelError& e)
    {
        std::cerr << "\nGoChannelError: " << e.what() << std::endl;
        status = -1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nError: " << e.what() << std::endl;
        status = -1;
    }

    kDestroyRef(&goApiLib);

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::ignore = getchar();

    return status;
}
