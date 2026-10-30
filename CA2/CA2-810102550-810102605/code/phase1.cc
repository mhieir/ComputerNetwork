#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-flow-classifier.h"

#include <cmath>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Wifi5Phase1");

constexpr uint32_t NUM_STATIONS = 5;
constexpr double DISTANCE_RADIUS = 5.0;
constexpr uint16_t PORT_NUMBER = 9;
constexpr double SIMULATION_TIME = 10.0;
constexpr double PACKET_INTERVAL = 0.5;
constexpr uint32_t MAX_PACKETS = 1000;
constexpr uint32_t LARGE_PACKET_SIZE = 1024;
constexpr uint32_t SMALL_PACKET_SIZE = 512;
const std::string SSID_NAME = "ns-3-ut-project";

std::pair<NetDeviceContainer, NetDeviceContainer> SetupWifiNetwork(NodeContainer& staNodes, NodeContainer& apNode) 
{
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    //phy.Set("RxNoiseFigure", DoubleValue(20.0));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);

    WifiMacHelper mac;
    Ssid ssid = Ssid(SSID_NAME);

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevices = wifi.Install(phy, mac, apNode);

    return {apDevices, staDevices};
}

void SetupMobility(NodeContainer& apNode, NodeContainer& staNodes) 
{
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    positionAlloc->Add(Vector(0.0, 0.0, 0.0));

    for (uint32_t i = 0; i < NUM_STATIONS; ++i) {
        double angle = i * (2.0 * M_PI) / NUM_STATIONS;
        positionAlloc->Add(Vector(DISTANCE_RADIUS * std::cos(angle), DISTANCE_RADIUS * std::sin(angle), 0.0));
    }

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNodes);
}

std::pair<Ipv4InterfaceContainer, Ipv4InterfaceContainer> SetupInternet(
    NodeContainer& apNode, NodeContainer& staNodes, NetDeviceContainer& apDevices, NetDeviceContainer& staDevices) 
{
    InternetStackHelper stack;
    stack.Install(apNode);
    stack.Install(staNodes);

    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");

    Ipv4InterfaceContainer apInterface = address.Assign(apDevices);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);

    return {apInterface, staInterfaces};
}

void SetupApplications(NodeContainer& apNode, NodeContainer& staNodes, Ipv4InterfaceContainer& apInterface) 
{

    UdpEchoServerHelper echoServer(PORT_NUMBER);
    ApplicationContainer serverApps = echoServer.Install(apNode.Get(0));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(SIMULATION_TIME));

    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        uint32_t packetSize = (i % 2 == 0) ? LARGE_PACKET_SIZE : SMALL_PACKET_SIZE; 
        UdpEchoClientHelper echoClient(apInterface.GetAddress(0), PORT_NUMBER);
        echoClient.SetAttribute("MaxPackets", UintegerValue(MAX_PACKETS));
        echoClient.SetAttribute("Interval", TimeValue(Seconds(PACKET_INTERVAL)));
        echoClient.SetAttribute("PacketSize", UintegerValue(packetSize));

        ApplicationContainer clientApps = echoClient.Install(staNodes.Get(i));
        clientApps.Start(Seconds(0.0));
        clientApps.Stop(Seconds(SIMULATION_TIME));
    }
}

void DisplayMetrics(Ptr<FlowMonitor> monitor, FlowMonitorHelper& flowmon, Ipv4InterfaceContainer& apInterface) 
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double sumThroughput = 0.0, sumThroughputSq = 0.0, sumDelay = 0.0;
    int numFlows = 0;

    std::cout << "\nIEEE 802.11ac (Wi-Fi 5) Baseline Results\n";
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

            sumThroughput += throughput;
            sumDelay += delay;
            sumThroughputSq += (throughput * throughput);
            numFlows++;
        }
    }

    if (numFlows > 0) {
        double jainsIndex = (sumThroughput * sumThroughput) / (numFlows * sumThroughputSq);
        std::cout << "\n--- Aggregate Metrics ---\n"
                  << "Average Throughput:    " << (sumThroughput / numFlows) << " Mbps\n"
                  << "Average Delay:         " << (sumDelay * 1000.0 / numFlows) << " ms\n"
                  << "Jain's Fairness Index: " << jainsIndex << "\n";
    }
}

int main(int argc, char *argv[]) 
{
    // LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    NodeContainer apNode, staNodes;
    apNode.Create(1);
    staNodes.Create(NUM_STATIONS);

    auto [apDevices, staDevices] = SetupWifiNetwork(staNodes, apNode);
    SetupMobility(apNode, staNodes);
    auto [apInterface, staInterfaces] = SetupInternet(apNode, staNodes, apDevices, staDevices);
    SetupApplications(apNode, staNodes, apInterface);

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(SIMULATION_TIME));
    Simulator::Run();

    DisplayMetrics(monitor, flowmon, apInterface);
    
    Simulator::Destroy();
    return 0;
}