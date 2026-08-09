// Phoenix headers (and the std library they pull in) must precede
// Arduino/bluefruit headers: the Arduino core defines min/max macros that
// would otherwise mangle the C++ standard headers.
#include "ble_glue.h"

#include <phoenix/protocol.h>
#include <phoenix/text_sanitize.h>

#include <bluefruit.h>

#include "fw_config.h"
#include "power.h"

namespace ble_glue {
namespace {

phoenix::Device* dev = nullptr;
SemaphoreHandle_t devMutex = nullptr;

// 128-bit UUIDs from PROTOCOL.md §1, as the SoftDevice wants them:
// little-endian byte arrays (string reads MSB-first, array stores reversed).
// CD310001-0101-4B2F-9456-982A27ED3560
const uint8_t kSvcUuid[16] = {0x60, 0x35, 0xED, 0x27, 0x2A, 0x98, 0x56, 0x94,
                              0x2F, 0x4B, 0x01, 0x01, 0x01, 0x00, 0x31, 0xCD};
// CD310002-... (TX, phone -> glasses, write / write-without-response)
const uint8_t kTxUuid[16] = {0x60, 0x35, 0xED, 0x27, 0x2A, 0x98, 0x56, 0x94,
                             0x2F, 0x4B, 0x01, 0x01, 0x02, 0x00, 0x31, 0xCD};
// CD310003-... (RX, glasses -> phone, notify)
const uint8_t kRxUuid[16] = {0x60, 0x35, 0xED, 0x27, 0x2A, 0x98, 0x56, 0x94,
                             0x2F, 0x4B, 0x01, 0x01, 0x03, 0x00, 0x31, 0xCD};

BLEService phxService(kSvcUuid);
BLECharacteristic phxTx(kTxUuid);
BLECharacteristic phxRx(kRxUuid);

BLEAncs bleancs;
BLEClientCts blects;

class DeviceLock {
 public:
  DeviceLock() { xSemaphoreTake(devMutex, portMAX_DELAY); }
  ~DeviceLock() { xSemaphoreGive(devMutex); }
};

// ---- ANCS ----

// Attribute fetch sizes: generous enough for the 72x40 display's appetite,
// small enough for the stack.
constexpr uint16_t kAppIdMax = 64;
constexpr uint16_t kTitleMax = 96;
constexpr uint16_t kMessageMax = 256;

void ancsNotificationCallback(AncsNotification_t* notif) {
  using namespace phoenix::ancs;
  if (notif == nullptr) return;

  AncsEvent e;
  e.event = static_cast<EventId>(notif->eventID);
  e.notification.uid = notif->uid;
  e.notification.category =
      notif->categoryID <= static_cast<uint8_t>(CategoryId::Entertainment)
          ? static_cast<CategoryId>(notif->categoryID)
          : CategoryId::Other;
  uint8_t flags = 0;
  if (notif->eventFlags.silent) flags |= kFlagSilent;
  if (notif->eventFlags.important) flags |= kFlagImportant;
  if (notif->eventFlags.preExisting) flags |= kFlagPreExisting;
  if (notif->eventFlags.positiveAction) flags |= kFlagPositiveAction;
  if (notif->eventFlags.NegativeAction) flags |= kFlagNegativeAction;
  e.notification.eventFlags = flags;

  if (e.event != EventId::Removed) {
    // Blocking attribute round-trips; we are on the callback task, which is
    // exactly where the Bluefruit ANCS example does the same.
    char buf[kMessageMax + 1];

    uint16_t n = bleancs.getAttribute(notif->uid, ANCS_ATTR_APP_IDENTIFIER,
                                      buf, kAppIdMax);
    buf[n < kAppIdMax ? n : kAppIdMax] = 0;
    e.notification.appIdentifier = buf;

    n = bleancs.getAttribute(notif->uid, ANCS_ATTR_TITLE, buf, kTitleMax);
    buf[n < kTitleMax ? n : kTitleMax] = 0;
    e.notification.title = buf;

    n = bleancs.getAttribute(notif->uid, ANCS_ATTR_SUBTITLE, buf, kTitleMax);
    buf[n < kTitleMax ? n : kTitleMax] = 0;
    e.notification.subtitle = buf;

    n = bleancs.getAttribute(notif->uid, ANCS_ATTR_MESSAGE, buf, kMessageMax);
    buf[n < kMessageMax ? n : kMessageMax] = 0;
    e.notification.message = buf;

    n = bleancs.getAttribute(notif->uid, ANCS_ATTR_DATE, buf, 24);
    buf[n < 24 ? n : 24] = 0;
    e.notification.date = buf;

    if (!e.notification.appIdentifier.empty()) {
      n = bleancs.getAppAttribute(e.notification.appIdentifier.c_str(),
                                  ANCS_APP_ATTR_DISPLAY_NAME, buf, kAppIdMax);
      buf[n < kAppIdMax ? n : kAppIdMax] = 0;
      e.notification.appDisplayName = buf;
    }
  }

  {
    DeviceLock lock;
    dev->onAncsEvent(e);
  }
  power::noteActivity();
}

// ---- CTS ----

void applyCtsTime() {
  // Sanity-gate the phone's answer; a rejected read just leaves the clock
  // on its previous (tick-advanced) time.
  const auto& t = blects.Time;
  if (t.year < 2000 || t.year > 2100 || t.month < 1 || t.month > 12 ||
      t.day < 1 || t.day > 31 || t.hour > 23 || t.minute > 59 ||
      t.second > 59) {
    return;
  }
  phoenix::DateTime dt;
  dt.year = t.year;
  dt.month = t.month;
  dt.day = t.day;
  dt.hour = t.hour;
  dt.minute = t.minute;
  dt.second = t.second;
  DeviceLock lock;
  dev->setTime(dt);
}

void ctsAdjustCallback(uint8_t reason) {
  (void)reason;  // manual change, timezone move, DST — all mean "resync"
  if (blects.getCurrentTime()) applyCtsTime();
}

// ---- Phoenix service ----

void txWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data,
                     uint16_t len) {
  (void)conn_hdl;
  (void)chr;
  {
    DeviceLock lock;
    dev->feedBle(data, len);
  }
  power::noteActivity();
}

// ---- Connection lifecycle ----

void connectCallback(uint16_t conn_handle) {
  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  // iOS decides the MTU; we can only ask (PROTOCOL.md §1).
  conn->requestMtuExchange(247);

  const bool ancsFound = bleancs.discover(conn_handle);
  if (ancsFound) blects.discover(conn_handle);

  // ANCS requires an encrypted, bonded link — and the Phoenix service
  // demands encryption too — so pairing starts immediately either way.
  conn->requestPairing();

  {
    DeviceLock lock;
    dev->setConnected(true);
  }
  power::noteActivity();
}

void connectionSecuredCallback(uint16_t conn_handle) {
  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  if (bleancs.discovered()) {
    bleancs.enableNotification();
  }
  if (blects.discovered()) {
    blects.enableAdjust();
    if (blects.getCurrentTime()) applyCtsTime();
  }
  {
    DeviceLock lock;
    dev->setBonded(conn->bonded());
  }
  power::noteActivity();
}

void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;
  DeviceLock lock;
  dev->setConnected(false);
}

}  // namespace

void begin(phoenix::Device* device, SemaphoreHandle_t deviceMutex) {
  dev = device;
  devMutex = deviceMutex;

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);  // room for a 247-byte MTU
  Bluefruit.begin();  // one peripheral link; client roles ride the same link
  Bluefruit.setTxPower(4);
  Bluefruit.setName(kDeviceName);
  Bluefruit.autoConnLed(false);  // every LED milliamp is battery life

  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);
  Bluefruit.Security.setSecuredCallback(connectionSecuredCallback);

  // GATT client roles (bond state persists in InternalFS across reboots,
  // handled by the Bluefruit stack).
  bleancs.begin();
  bleancs.setNotificationCallback(ancsNotificationCallback);
  blects.begin();
  blects.setAdjustCallback(ctsAdjustCallback);

  // Phoenix service: both characteristics demand an encrypted link.
  phxService.begin();

  phxTx.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  phxTx.setPermission(SECMODE_NO_ACCESS, SECMODE_ENC_NO_MITM);
  phxTx.setMaxLen(244);  // one full 247-MTU write
  phxTx.setWriteCallback(txWriteCallback);
  phxTx.begin();

  phxRx.setProperties(CHR_PROPS_NOTIFY);
  phxRx.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
  phxRx.setMaxLen(244);
  phxRx.begin();

  // Advertise the Phoenix service; soliciting ANCS (addService on a client
  // service emits a solicitation UUID) nudges iOS to surface the accessory.
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(phxService);
  Bluefruit.Advertising.addService(bleancs);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);  // 20 ms fast / 152.5 ms slow
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void service() {
  std::vector<uint8_t> out;
  std::vector<phoenix::Device::AncsActionRequest> actions;
  {
    DeviceLock lock;
    out = dev->takeOutbox();
    actions = dev->takeAncsActions();
  }

  // ANCS actions (call accept/decline) go out as client commands.
  for (const auto& a : actions) {
    if (bleancs.discovered()) {
      bleancs.performAction(
          a.uid, a.positive ? ANCS_ACTION_POSITIVE : ANCS_ACTION_NEGATIVE);
    }
  }

  if (out.empty() || !Bluefruit.connected()) return;
  BLEConnection* conn = Bluefruit.Connection(Bluefruit.connHandle());
  if (conn == nullptr || !phxRx.notifyEnabled()) return;

  // Frames are a byte stream (PROTOCOL.md §1): slice by the negotiated MTU;
  // the phone's decoder reassembles.
  const uint16_t mtu = conn->getMtu();
  const size_t chunk = mtu > 3 ? static_cast<size_t>(mtu - 3) : 20;
  size_t i = 0;
  while (i < out.size()) {
    const size_t n = out.size() - i < chunk ? out.size() - i : chunk;
    phxRx.notify(out.data() + i, static_cast<uint16_t>(n));
    i += n;
  }
}

bool connected() { return Bluefruit.connected(); }

}  // namespace ble_glue
