
#include "hal/hci_hal.h"
#include "hal/snoop_logger.h"
#include "hci_hal_windows.h"

#include <future>
#include <functional>

constexpr uint8_t kH4Command = 0x01;
constexpr uint8_t kH4Acl = 0x02;
constexpr uint8_t kH4Sco = 0x03;
constexpr uint8_t kH4Event = 0x04;
constexpr uint8_t kH4Iso = 0x05;

constexpr uint8_t kH4HeaderSize = 1;
constexpr uint8_t kHciAclHeaderSize = 4;
constexpr uint8_t kHciScoHeaderSize = 3;
constexpr uint8_t kHciEvtHeaderSize = 2;
constexpr uint8_t kHciIsoHeaderSize = 4;
constexpr int kBufSize = 1024 + 4 + 1;  // DeviceProperties::acl_data_packet_size_ + ACL header + H4 header

constexpr uint8_t BTPROTO_HCI = 1;
constexpr uint16_t HCI_CHANNEL_USER = 1;
constexpr uint16_t HCI_CHANNEL_CONTROL = 3;
constexpr uint16_t HCI_DEV_NONE = 0xffff;

#ifndef ssize_t
#define ssize_t int64_t
#endif

extern void hci_initialize(std::promise<void> initial_promise,
    std::function<void(char, char*, uint16_t)> call_back);
extern bool hci_transmit
    (
    char a_type,    // The packet type. Command, event, or acl packet
    char* a_buffer, // Packet buffer
    uint16_t a_size // Packet size
    );

namespace bluetooth {
namespace hal {

  HciHalImpl::HciHalImpl( os::Handler*, LinkClocker&, SnoopLogger* a_btsnoop_logger_ )
  {
    btsnoop_logger_ = a_btsnoop_logger_;
    Start();
  }

  void HciHalImpl::registerIncomingPacketCallback(HciHalCallbacks* callback) {
    std::lock_guard<std::mutex> lock(api_mutex_);
    log::info("{} before", __func__);
    {
      std::lock_guard<std::mutex> incoming_packet_callback_lock(incoming_packet_callback_mutex_);
      log::assert_that(incoming_packet_callback_ == nullptr && callback != nullptr, 
                        "incoming_packet_callback_ == nullptr && callback != nullptr");
      incoming_packet_callback_ = callback;
    }
    log::info("{} after", __func__);
  }

  void HciHalImpl::unregisterIncomingPacketCallback() {
    std::lock_guard<std::mutex> lock(api_mutex_);
    log::info( "{} before", __func__ );
    {
      std::lock_guard<std::mutex> incoming_packet_callback_lock(incoming_packet_callback_mutex_);
      incoming_packet_callback_ = nullptr;
    }
    log::info("{} after", __func__);
  }

  void HciHalImpl::sendHciCommand(HciPacket command) {
    std::lock_guard<std::mutex> lock(api_mutex_);
    std::vector<uint8_t> packet = std::move(command);
    btsnoop_logger_->Capture(packet, SnoopLogger::Direction::OUTGOING, SnoopLogger::PacketType::CMD);
    send_data_to_controller(kH4Command,std::move(packet));
  }

  void HciHalImpl::sendAclData(HciPacket data) {
    std::lock_guard<std::mutex> lock(api_mutex_);
    std::vector<uint8_t> packet = std::move(data);
    btsnoop_logger_->Capture(packet, SnoopLogger::Direction::OUTGOING, SnoopLogger::PacketType::ACL);
    send_data_to_controller(kH4Acl,std::move(packet));
  }

  void HciHalImpl::sendScoData(HciPacket data) {
    std::lock_guard<std::mutex> lock(api_mutex_);
    std::vector<uint8_t> packet = std::move(data);
    btsnoop_logger_->Capture(packet, SnoopLogger::Direction::OUTGOING, SnoopLogger::PacketType::SCO);
    send_data_to_controller(kH4Sco,std::move(packet));
  }

  void HciHalImpl::sendIsoData(HciPacket data) {
    std::lock_guard<std::mutex> lock(api_mutex_);
    std::vector<uint8_t> packet = std::move(data);
    btsnoop_logger_->Capture(packet, SnoopLogger::Direction::OUTGOING, SnoopLogger::PacketType::ISO);
    send_data_to_controller(kH4Iso,std::move(packet));
  }

  void HciHalImpl::Start()
  {
    std::promise<void> init_promise;
    std::future<void> init_future = init_promise.get_future();
    hci_initialize(
      std::move( init_promise ),
      std::bind(
        &HciHalImpl::handle_data_from_chip,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3 ) );

    init_future.wait();
    log::info( "HAL opened successfully" );
  }

  void HciHalImpl::send_data_to_controller(char type, std::vector<uint8_t> pkt) {
     hci_transmit(type, reinterpret_cast<char*>( pkt.data() ),pkt.size());
  }

  void HciHalImpl::handle_data_from_chip(char type, char* buffer, uint16_t size)
  {
    if (incoming_packet_callback_ == nullptr) {
      log::info("Dropping an event after processing");
      return;
    }

    HciPacket receivedHciPacket;
    uint8_t* p = reinterpret_cast<uint8_t*>(buffer);
    receivedHciPacket.assign( p, p + size);
    SnoopLogger::PacketType pkt_type;

    std::lock_guard<std::mutex> incoming_packet_callback_lock(incoming_packet_callback_mutex_);
    if (type == kH4Event) {
       pkt_type = SnoopLogger::PacketType::EVT;
        btsnoop_logger_->Capture(receivedHciPacket, SnoopLogger::Direction::INCOMING, pkt_type);
       incoming_packet_callback_->hciEventReceived(receivedHciPacket);
    }
    else if (type == kH4Acl) {
        pkt_type = SnoopLogger::PacketType::ACL;
        btsnoop_logger_->Capture(receivedHciPacket, SnoopLogger::Direction::INCOMING, pkt_type);
        incoming_packet_callback_->aclDataReceived(receivedHciPacket);
    }
    else if (type == kH4Sco) {
        pkt_type = SnoopLogger::PacketType::SCO;
        btsnoop_logger_->Capture(receivedHciPacket, SnoopLogger::Direction::INCOMING, pkt_type);
        incoming_packet_callback_->scoDataReceived(receivedHciPacket);
    }
    else
    {
        LOG(ERROR) << "Unknown data packet. type:" << uint32_t(type);
        return;
    }

  }
}
}  // namespace bluetooth

