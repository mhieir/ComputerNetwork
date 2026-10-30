#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/spectrum-module.h"

#include <cmath>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Wifi6Phase2");


constexpr uint32_t NUM_STATIONS =5;
constexpr double DISTANCE_FROM_AP = 5.0;
constexpr uint16_t PORT_NUMBER = 9;
constexpr double SIMULATION_TIME = 10.0;
constexpr double PACKET_INTERVAL = 0.5;        
constexpr uint32_t MAX_PACKETS = 20;           
constexpr uint32_t LARGE_PACKET_SIZE = 1024;        
constexpr uint32_t SMALL_PACKET_SIZE = 512;         
constexpr uint32_t TX_ANTENNAS = 4;
constexpr uint32_t RX_ANTENNAS = 4;
const std::string SSID_NAME = "wifi6-network";
const std::string IP_BASE = "192.168.1.0";
const std::string IP_MASK = "255.255.255.0";

std::pair<NetDeviceContainer, NetDeviceContainer> SetupWifiNetwork(NodeContainer& staNodes, NodeContainer& apNode)
{
    SpectrumWifiPhyHelper phy;
    SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default();
    channelHelper.SetChannel("ns3::MultiModelSpectrumChannel");
    phy.SetChannel(channelHelper.Create());

    // MU-MIMO Configuration
    phy.Set("Antennas", UintegerValue(4));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(TX_ANTENNAS));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(RX_ANTENNAS));
    phy.Set("ChannelSettings", StringValue("{38, 40, BAND_5GHZ, 0}"));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("HeMcs7"), 
                                 "ControlMode", StringValue("HeMcs5")); 

    WifiMacHelper mac;
    Ssid ssid = Ssid(SSID_NAME);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    mac.SetMultiUserScheduler("ns3::RrMultiUserScheduler",
                              "EnableUlOfdma", BooleanValue(true),
                              "EnableBsrp", BooleanValue(true),
                              "NStations", UintegerValue(8), 
                              "UseCentral26TonesRus", BooleanValue(false));

    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    return {apDevice, staDevices};
}

void SetupMobility(NodeContainer& apNode, NodeContainer& staNodes)
{
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNodes);

    Ptr<MobilityModel> apPos = apNode.Get(0)->GetObject<MobilityModel>();
    apPos->SetPosition(Vector(0.0, 0.0, 0.0));

    for (uint32_t i = 0; i < NUM_STATIONS; ++i) {
        double angle = i * (2.0 * M_PI) / NUM_STATIONS;
        Ptr<MobilityModel> staPos = staNodes.Get(i)->GetObject<MobilityModel>();
        staPos->SetPosition(Vector(DISTANCE_FROM_AP * cos(angle), DISTANCE_FROM_AP * sin(angle), 0.0));
    }
}

std::pair<Ipv4InterfaceContainer, Ipv4InterfaceContainer> SetupInternet(
    NodeContainer& apNode, NodeContainer& staNodes, NetDeviceContainer& apDevice, NetDeviceContainer& staDevices)
{
    InternetStackHelper stack;
    stack.Install(apNode);
    stack.Install(staNodes);

    Ipv4AddressHelper address;
    address.SetBase(IP_BASE.c_str(), IP_MASK.c_str());

    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);

    return {apInterface, staInterfaces};
}

void SetupApplications(NodeContainer& apNode, NodeContainer& staNodes, Ipv4InterfaceContainer& apInterface)
{
    UdpEchoServerHelper server(PORT_NUMBER);
    ApplicationContainer serverApp = server.Install(apNode.Get(0));
    serverApp.Start(Seconds(0.0));
    serverApp.Stop(Seconds(SIMULATION_TIME));

    for (uint32_t i = 0; i < NUM_STATIONS; ++i) {
        UdpEchoClientHelper client(apInterface.GetAddress(0), PORT_NUMBER);
        uint32_t packetSize = (i % 2 == 0) ? LARGE_PACKET_SIZE : SMALL_PACKET_SIZE;

        client.SetAttribute("PacketSize", UintegerValue(packetSize));
        client.SetAttribute("Interval", TimeValue(Seconds(PACKET_INTERVAL)));
        client.SetAttribute("MaxPackets", UintegerValue(MAX_PACKETS));

        ApplicationContainer clientApp = client.Install(staNodes.Get(i));
        clientApp.Start(Seconds(0.0));
        clientApp.Stop(Seconds(SIMULATION_TIME));
    }
}

void DisplayMetrics(Ptr<FlowMonitor> monitor, FlowMonitorHelper& flowmon, Ipv4InterfaceContainer& apInterface) 
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double sumThroughput = 0.0, sumThroughputSq = 0.0, sumDelay = 0.0;
    int numFlows = 0;

    std::cout << "\nIEEE 802.11ax (Wi-Fi 6) OFDMA + MU-MIMO Results\n";
    for (const auto& flow : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);

        if (t.destinationAddress == apInterface.GetAddress(0)) {
            double rxDuration = flow.second.timeLastRxPacket.GetSeconds() - flow.second.timeFirstTxPacket.GetSeconds();
            double throughput = (rxDuration > 0) ? (flow.second.rxBytes * 8.0) / rxDuration / 1e6 : 0.0; 
            double delay = (flow.second.rxPackets > 0) ? flow.second.delaySum.GetSeconds() / flow.second.rxPackets : 0.0;
            double lossRate = (flow.second.txPackets > 0) ? ((double)(flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets) * 100 : 0.0;

            std::cout << "Flow " << flow.first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n"
                      << "  Throughput: " << std::fixed << std::setprecision(3) << throughput << " Mbps\n"
                      << "  Avg Delay:  " << delay * 1000.0 << " ms\n"
                      << "  Loss Rate:  " << lossRate << "%\n\n";

            sumDelay += delay * 1000.0;
            sumThroughput += throughput;
            sumThroughputSq += (throughput * throughput);
            numFlows++;
        }
    }

    if (numFlows > 0) {
        double jainsIndex = (sumThroughput * sumThroughput) / (numFlows * sumThroughputSq);
        std::cout << "\n--- Aggregate Metrics ---\n"
                  << "Average Throughput:    " << (sumThroughput / numFlows) << " Mbps\n"
                  << "Average Delay:         " << (sumDelay / numFlows) << " ms\n"
                  << "Jain's Fairness Index: " << jainsIndex << "\n";
    }
}

int main(int argc, char *argv[])
{
    NodeContainer staNodes, apNode;
    staNodes.Create(NUM_STATIONS);
    apNode.Create(1);

    auto [apDevice, staDevices] = SetupWifiNetwork(staNodes, apNode);
    SetupMobility(apNode, staNodes);
    auto [apInterface, staInterfaces] = SetupInternet(apNode, staNodes, apDevice, staDevices);
    SetupApplications(apNode, staNodes, apInterface);

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(SIMULATION_TIME));
    Simulator::Run();

    DisplayMetrics(flowMonitor, flowmonHelper, apInterface);
    
    Simulator::Destroy();
    return 0;
}