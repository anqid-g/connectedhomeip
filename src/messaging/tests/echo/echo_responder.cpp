/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file implements a chip-echo-responder, for the
 *      CHIP Echo Protocol.
 *
 *      The CHIP Echo Protocol implements two simple methods, in the
 *      style of ICMP ECHO REQUEST and ECHO REPLY, in which a sent
 *      payload is turned around by the responder and echoed back to
 *      the originator.
 *
 */

#include "common.h"

#include <core/CHIPCore.h>
#include <platform/CHIPDeviceLayer.h>
#include <protocols/echo/Echo.h>
#include <protocols/secure_channel/PASESession.h>
#include <protocols/user_directed_commissioning/UserDirectedCommissioning.h>
#include <support/ErrorStr.h>
#include <system/SystemPacketBuffer.h>
#include <transport/raw/TCP.h>
#include <transport/raw/UDP.h>

namespace {

// The EchoServer object.
chip::Protocols::Echo::EchoServer gEchoServer;
chip::Protocols::UserDirectedCommissioning::UserDirectedCommissioningServer gUDCServer =
    chip::Platform::New<chip::Protocols::UserDirectedCommissioning::UserDirectedCommissioningServer>();
chip::TransportMgr<chip::Transport::UDP> gUDPManager; // for Echo Traffic
chip::TransportMgr<chip::Transport::UDP> gUDCManager; // for User Directed Commissioning
chip::TransportMgr<chip::Transport::TCP<kMaxTcpActiveConnectionCount, kMaxTcpPendingPackets>> gTCPManager;
chip::SecurePairingUsingTestSecret gTestPairing;

// Callback handler when a CHIP EchoRequest is received.
void HandleEchoRequestReceived(chip::Messaging::ExchangeContext * ec, chip::System::PacketBufferHandle && payload)
{
    payload->DebugDump("HandleEchoRequestReceived Echo Request ... sending response.");
}

class DLL_EXPORT UDCListener : public chip::Protocols::UserDirectedCommissioning::InstanceNameResolver
{
public:
    void FindCommissionableNode(char * instanceName) override;
};

void UDCListener::FindCommissionableNode(char * instanceName)
{
    printf("FindCommissionableNode instanceName=%s\n", instanceName);
}

UDCListener gListener;

} // namespace

int main(int argc, char * argv[])
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::Optional<chip::Transport::PeerAddress> peer(chip::Transport::Type::kUndefined);
    bool useTCP      = false;
    bool disableEcho = false;
    bool disableUDC  = false;

    chip::Transport::AdminPairingTable admins;
    chip::Transport::AdminPairingInfo * adminInfo = nullptr;

    const chip::Transport::AdminId gAdminId = 0;

    if (argc > 2)
    {
        printf("Too many arguments specified!\n");
        ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    if ((argc == 2) && (strcmp(argv[1], "--tcp") == 0))
    {
        useTCP = true;
    }

    if ((argc == 2) && (strcmp(argv[1], "--disable-echo") == 0))
    {
        disableEcho = true;
    }

    if ((argc == 2) && (strcmp(argv[1], "--disable-UDC") == 0))
    {
        disableUDC = true;
    }

    InitializeChip();

    adminInfo = admins.AssignAdminId(gAdminId, chip::kTestDeviceNodeId);
    VerifyOrExit(adminInfo != nullptr, err = CHIP_ERROR_NO_MEMORY);

    if (useTCP)
    {
        err = gTCPManager.Init(
            chip::Transport::TcpListenParameters(&chip::DeviceLayer::InetLayer).SetAddressType(chip::Inet::kIPAddressType_IPv4));
        SuccessOrExit(err);

        err = gSessionManager.Init(chip::kTestDeviceNodeId, &chip::DeviceLayer::SystemLayer, &gTCPManager, &admins,
                                   &gMessageCounterManager);
        SuccessOrExit(err);
    }
    else
    {
        err = gUDPManager.Init(chip::Transport::UdpListenParameters(&chip::DeviceLayer::InetLayer)
                                   .SetAddressType(chip::Inet::kIPAddressType_IPv4)
                                   .SetListenPort(CHIP_PORT));
        SuccessOrExit(err);

        err = gSessionManager.Init(chip::kTestDeviceNodeId, &chip::DeviceLayer::SystemLayer, &gUDPManager, &admins,
                                   &gMessageCounterManager);
        SuccessOrExit(err);
    }

    err = gExchangeManager.Init(&gSessionManager);
    SuccessOrExit(err);

    err = gMessageCounterManager.Init(&gExchangeManager);
    SuccessOrExit(err);

    if (!disableEcho)
    {
        err = gEchoServer.Init(&gExchangeManager);
        SuccessOrExit(err);
    }

    if (!disableUDC)
    {
        gUDCManager.Init(chip::Transport::UdpListenParameters(&chip::DeviceLayer::InetLayer)
                             .SetAddressType(chip::Inet::kIPAddressType_IPv4)
                             .SetListenPort(CHIP_PORT + 3));

        gUDCManager.SetSecureSessionMgr(&gUDCServer);

        SuccessOrExit(err);
    }

    err = gSessionManager.NewPairing(peer, chip::kTestControllerNodeId, &gTestPairing, chip::SecureSession::SessionRole::kResponder,
                                     gAdminId);
    SuccessOrExit(err);

    if (!disableEcho)
    {
        // Arrange to get a callback whenever an Echo Request is received.
        gEchoServer.SetEchoRequestReceived(HandleEchoRequestReceived);
        printf("Listening for Echo requests...\n");
    }

    if (!disableUDC)
    {
        // Arrange to get a callback whenever an Echo Request is received.
        gUDCServer.SetInstanceNameResolver(&gListener);
        printf("Listening for UDC requests...\n");
    }

    chip::DeviceLayer::PlatformMgr().RunEventLoop();

exit:
    if (err != CHIP_NO_ERROR)
    {
        printf("EchoServer failed, err:%s\n", chip::ErrorStr(err));
        exit(EXIT_FAILURE);
    }

    if (!disableEcho)
    {
        gEchoServer.Shutdown();
    }

    ShutdownChip();

    return EXIT_SUCCESS;
}
