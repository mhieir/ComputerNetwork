#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/spectrum-module.h"
#include <map>
#include <iomanip>

using namespace ns3;

constexpr uint32_t NUM_STATIONS = 40;
constexpr double MIN_DISTANCE = 5.0;
constexpr double MAX_DISTANCE = 30.0;
constexpr double AP_HEIGHT = 3.0;
constexpr double STA_HEIGHT = 1.0;
constexpr uint16_t PORT_NUMBER = 9;
constexpr double SIMULATION_TIME = 10.0;
constexpr double PACKET_INTERVAL = 0.005;
constexpr uint32_t PACKET_SIZE = 93;
const std::string SSID_NAME = "wifi7-mlo-network"; 
const std::string IP_BASE = "192.168.1.0";
const std::string IP_MASK = "255.255.255.0";

std::map<Mac48Address, double> g_stationSinrSum;
std::map<Mac48Address, uint32_t> g_stationRxCount;

void MonitorSnifferRxCallback(std::string context, Ptr<const Packet> packet, uint16_t channelFreqMhz, 
                              WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise, uint16_t staId)
{
    double signalLinear = std::pow(10.0, signalNoise.signal / 10.0);
    double noiseLinear = std::pow(10.0, signalNoise.noise / 10.0);
    double sinrLinear = signalLinear / noiseLinear;

    Ptr<Packet> copy = packet->Copy();
    uint8_t buffer[2];
    copy->CopyData(buffer, 2);
    uint16_t frameControl = buffer[0] | (buffer[1] << 8);
    uint8_t type = (frameControl >> 2) & 0x3;

    if (type != 0x00 && type != 0x02) return;

    WifiMacHeader hdr;
    copy->RemoveHeader(hdr);
    Mac48Address srcAddr = hdr.GetAddr2();

    if (srcAddr == Mac48Address("00:00:00:00:00:00") || srcAddr == Mac48Address("00:00:00:01:00:00")) return;

    g_stationSinrSum[srcAddr] += sinrLinear;
    g_stationRxCount[srcAddr]++;
}

std::pair<NetDeviceContainer, NetDeviceContainer> SetupWifiNetwork(NodeContainer& staNodes, NodeContainer& apNode)
{
    SpectrumWifiPhyHelper phy;
    SpectrumChannelHelper channelHelper = SpectrumChannelHelper::Default();
    channelHelper.SetChannel("ns3::MultiModelSpectrumChannel");
    phy.SetChannel(channelHelper.Create());
    phy.Set("ChannelSettings", StringValue("{38, 40, BAND_5GHZ, 0}"));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211be); 
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("EhtMcs5"),   // EHT MCS for Wi-Fi 7
                                 "ControlMode", StringValue("EhtMcs2"));

    WifiMacHelper mac;
    Ssid ssid = Ssid(SSID_NAME);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    mac.SetMultiUserScheduler("ns3::RrMultiUserScheduler",
                              "EnableUlOfdma", BooleanValue(true),
                              "EnableBsrp", BooleanValue(true),
                              "UseCentral26TonesRus", BooleanValue(true),
                              "NStations", UintegerValue(8));

    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    return {apDevice, staDevices};
}

void SetupMobility(NodeContainer& apNode, NodeContainer& staNodes)
{
    MobilityHelper apMobility;
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> apAlloc = CreateObject<ListPositionAllocator>();
    apAlloc->Add(Vector(0.0, 0.0, AP_HEIGHT));
    apMobility.SetPositionAllocator(apAlloc);
    apMobility.Install(apNode);

    MobilityHelper staMobility;
    staMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<RandomDiscPositionAllocator> staAlloc = CreateObject<RandomDiscPositionAllocator>();
    staAlloc->SetX(0.0);
    staAlloc->SetY(0.0);
    staAlloc->SetRho(CreateObjectWithAttributes<UniformRandomVariable>("Min", DoubleValue(MIN_DISTANCE), "Max", DoubleValue(MAX_DISTANCE)));
    staMobility.SetPositionAllocator(staAlloc);
    staMobility.Install(staNodes);
    
    for (uint32_t i = 0; i < NUM_STATIONS; ++i) {
        Ptr<MobilityModel> mm = staNodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mm->GetPosition();
        pos.z = STA_HEIGHT;
        mm->SetPosition(pos);
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
    return {address.Assign(apDevice), address.Assign(staDevices)};
}

void SetupApplications(NodeContainer& apNode, NodeContainer& staNodes, Ipv4InterfaceContainer& apInterface)
{
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), PORT_NUMBER));
    ApplicationContainer sinkApp = sinkHelper.Install(apNode.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(SIMULATION_TIME));

    Ptr<UniformRandomVariable> randomStart = CreateObject<UniformRandomVariable>();
    randomStart->SetAttribute("Min", DoubleValue(0.0));
    randomStart->SetAttribute("Max", DoubleValue(0.05));

    for (uint32_t i = 0; i < NUM_STATIONS; ++i) {
        UdpClientHelper client(apInterface.GetAddress(0), PORT_NUMBER);
        client.SetAttribute("PacketSize", UintegerValue(PACKET_SIZE));
        client.SetAttribute("Interval", TimeValue(Seconds(PACKET_INTERVAL)));
        client.SetAttribute("MaxPackets", UintegerValue(100000));

        ApplicationContainer clientApp = client.Install(staNodes.Get(i));
        clientApp.Start(Seconds(randomStart->GetValue()));
        clientApp.Stop(Seconds(SIMULATION_TIME));
    }
}

void DisplayMetrics(Ptr<FlowMonitor> flowMonitor, FlowMonitorHelper& flowmonHelper)
{
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    double totalThroughput = 0.0, totalDelay = 0.0;
    double totalTx = 0.0, totalRx = 0.0, totalLost = 0.0;
    int numFlows = 0;
    std::vector<double> throughputs;

    std::cout << "\n===== Phase 4: Wi-Fi 7 (802.11be) Advanced Flow Statistics =====\n";
    
    for (const auto& flow : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        double txPackets = flow.second.txPackets;
        double rxPackets = flow.second.rxPackets;
        double lostPackets = txPackets - rxPackets;
        
        double duration = flow.second.timeLastRxPacket.GetSeconds() - flow.second.timeFirstTxPacket.GetSeconds();
        double throughput = (duration > 0 && rxPackets > 0) ? flow.second.rxBytes * 8.0 / duration / 1000.0 : 0.0;
        double avgDelay = (rxPackets > 0) ? flow.second.delaySum.GetSeconds() / rxPackets : 0.0;

        std::cout << "Flow " << numFlows + 1 << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n"
                  << "  Tx: " << txPackets << "  Rx: " << rxPackets << "  Lost: " << lostPackets 
                  << " (" << (txPackets > 0 ? (lostPackets / txPackets) * 100.0 : 0.0) << "%)\n"
                  << "  Throughput: " << throughput << " kbps  Delay: " << avgDelay * 1000.0 << " ms\n\n";

        throughputs.push_back(throughput);
        totalThroughput += throughput;
        totalDelay += avgDelay;
        totalTx += txPackets;
        totalRx += rxPackets;
        totalLost += lostPackets;
        numFlows++;
    }

    double sum = 0.0, sumSq = 0.0;
    for (double t : throughputs) { sum += t; sumSq += t * t; }
    
    std::cout 
              << "PHASE 4 SUMMARY\n"
              << "Flows Evaluated: " << numFlows << "\n"
              << "Total Tx: " << totalTx << " | Rx: " << totalRx << " | Lost: " << totalLost 
              << " (" << (totalTx > 0 ? (totalLost / totalTx) * 100.0 : 0.0) << "%)\n"
              << "Average Throughput: " << (numFlows > 0 ? totalThroughput / numFlows : 0.0) << " kbps\n"
              << "Total Throughput: " << totalThroughput << " kbps\n"
              << "Average Delay: " << (numFlows > 0 ? totalDelay / numFlows : 0.0) * 1000.0 << " ms\n"
              << "Jain's Fairness Index: " << ((numFlows > 0 && sumSq > 0) ? (sum * sum) / (numFlows * sumSq) : 0.0) << "\n";
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

    std::string apTracePath = "/NodeList/" + std::to_string(NUM_STATIONS) + "/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx";
    Config::Connect(apTracePath, MakeCallback(&MonitorSnifferRxCallback));

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();

    std::cout << "\nRunning Phase 4 (Wi-Fi 7) simulation for " << SIMULATION_TIME << " seconds...\n";

    Simulator::Stop(Seconds(SIMULATION_TIME));
    Simulator::Run();

    DisplayMetrics(flowMonitor, flowmonHelper);
    
    Simulator::Destroy();
    return 0;
}