#pragma once

#include "hal/hci_hal.h"
#include "hal/snoop_logger.h"
#include "hal/link_clocker.h"

#include <mutex>

#include <bluetooth/log.h>

namespace bluetooth {
namespace hal {

class HciHalImpl : public HciHal {
 public:
  
  HciHalImpl(os::Handler*, LinkClocker&, SnoopLogger*);

  void registerIncomingPacketCallback( HciHalCallbacks* callback ) override;

  void unregisterIncomingPacketCallback() override;

  void sendHciCommand( HciPacket command ) override;

  void sendAclData( HciPacket data ) override;

  void sendScoData( HciPacket data ) override;

  void sendIsoData( HciPacket data ) override;

 protected:

 private:
  // Held when APIs are called, NOT to be held during callbacks
  std::mutex api_mutex_;
  HciHalCallbacks* incoming_packet_callback_ = nullptr;
  std::mutex incoming_packet_callback_mutex_;
  SnoopLogger* btsnoop_logger_ = nullptr;

  void Start();

  void send_data_to_controller( char type, std::vector<uint8_t> pkt );

  void handle_data_from_chip( char type, char* buffer, uint16_t size );

};
}
}  // namespace bluetooth
