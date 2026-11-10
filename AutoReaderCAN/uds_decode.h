#pragma once

// ---------------------------------------------------------------------------
//  Пассивный монитор CAN-шины + декодер UDS (ISO 14229) поверх ISO-TP
//  (ISO 15765-2) для ESPHome / ESP32 + canbus.
//
//  Задача: видеть ВСЕ кадры на шине (какие ID/блоки шлют данные),
//  а диагностический обмен дополнительно разбирать как UDS
//  (в режиме LISTENONLY ничего не передаётся).
//
//  Возможности:
//   - живой лог кадров по каждому ID (с ограничением частоты);
//   - периодическая сводная таблица: ID, количество, период, последние данные;
//   - счётчик уникальных ID и кадров/сек;
//   - сборка многокадровых ISO-TP сообщений и расшифровка UDS.
//
//  Подключение в YAML:
//      esphome:
//        includes: [uds_decode.h]
//      ...
//      - lambda: |-
//          uds_decode::monitor().on_frame(can_id, false, remote_transmission_request,
//                                         x.data(), x.size());
//      - lambda: |-
//          uds_decode::monitor().begin(false, 1000, 10000);
//
//  ВАЖНО: все вызовы идут из главного цикла ESPHome (on_frame + interval),
//  поэтому мьютексы не нужны. Если начнёте звать из другого потока -
//  добавьте защиту.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace uds_decode {

static const char *const TAG = "uds";

// ---------------------------- настройки ------------------------------------

// Максимальная длина одного UDS-сообщения (ISO-TP classic, 12 бит длины).
// 512 байт с запасом хватает на VIN, списки DTC и чтение DID.
static const size_t TP_MAX_MESSAGE = 512;
// Таймаут между кадрами одного сообщения (N_Cr по ISO 15765-2 = 1000 мс).
static const uint32_t TP_TIMEOUT_MS = 1000;
// Сколько разных CAN-ID может собираться одновременно (0x7E0..0x7E7 / 0x7E8..0x7EF).
static const size_t TP_MAX_SESSIONS = 8;

// ---------------------------- монитор шины ---------------------------------

static const char *const TAG_BUS = "can_monitor";
// Максимум отслеживаемых ID (стандартные и расширенные вместе).
static const size_t MON_MAX_IDS = 128;
// Через сколько миллисекунд ID считается "пропавшим" (не показывается в таблице).
static const uint32_t MON_ID_EXPIRE_MS = 30000;

// ---------------------------- утилиты --------------------------------------

static inline void append(char *out, size_t out_n, size_t &pos, const char *fmt, ...) {
  if (pos + 1 >= out_n)
    return;
  va_list args;
  va_start(args, fmt);
  int written = std::vsnprintf(out + pos, out_n - pos, fmt, args);
  va_end(args);
  if (written > 0)
    pos += std::min<size_t>(static_cast<size_t>(written), out_n - 1 - pos);
}

static inline void append_hex(char *out, size_t out_n, size_t &pos, const uint8_t *data, size_t len,
                              size_t max_bytes = 16) {
  for (size_t i = 0; i < len && i < max_bytes; i++)
    append(out, out_n, pos, i == 0 ? "%02X" : " %02X", data[i]);
  if (len > max_bytes)
    append(out, out_n, pos, " ..");
}

static inline bool is_printable(const uint8_t *data, size_t len) {
  if (len == 0)
    return false;
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x20 || data[i] > 0x7E)
      return false;
  }
  return true;
}

static inline void append_value(char *out, size_t out_n, size_t &pos, const uint8_t *data, size_t len) {
  if (is_printable(data, len)) {
    append(out, out_n, pos, "\"%.*s\"", static_cast<int>(len), reinterpret_cast<const char *>(data));
  } else {
    append_hex(out, out_n, pos, data, len);
  }
}

// ---------------------------- имена ----------------------------------------

static inline const char *session_name(uint8_t session) {
  switch (session) {
    case 0x01:
      return "default";
    case 0x02:
      return "programming";
    case 0x03:
      return "extended";
    case 0x04:
      return "safety";
    default:
      return "unknown";
  }
}

static inline const char *service_name(uint8_t service) {
  switch (service) {
    // OBD-II (ISO 15031-5), ходит по тем же ID 0x7E0/0x7E8
    case 0x01:
      return "OBD showCurrentData";
    case 0x02:
      return "OBD showFreezeFrame";
    case 0x03:
      return "OBD readStoredDTC";
    case 0x04:
      return "OBD clearDTC";
    case 0x05:
      return "OBD oxygenSensorResults";
    case 0x06:
      return "OBD testResults";
    case 0x07:
      return "OBD readPendingDTC";
    case 0x08:
      return "OBD controlOnboardSystems";
    case 0x09:
      return "OBD requestVehicleInfo";
    case 0x0A:
      return "OBD readPermanentDTC";
    case 0x10:
      return "DiagnosticSessionControl";
    case 0x11:
      return "ECUReset";
    case 0x14:
      return "ClearDiagnosticInformation";
    case 0x19:
      return "ReadDTCInformation";
    case 0x22:
      return "ReadDataByIdentifier";
    case 0x23:
      return "ReadMemoryByAddress";
    case 0x24:
      return "ReadScalingDataByIdentifier";
    case 0x27:
      return "SecurityAccess";
    case 0x28:
      return "CommunicationControl";
    case 0x2A:
      return "ReadDataByPeriodicIdentifier";
    case 0x2C:
      return "DynamicallyDefineDataIdentifier";
    case 0x2E:
      return "WriteDataByIdentifier";
    case 0x2F:
      return "InputOutputControlByIdentifier";
    case 0x31:
      return "RoutineControl";
    case 0x34:
      return "RequestDownload";
    case 0x35:
      return "RequestUpload";
    case 0x36:
      return "TransferData";
    case 0x37:
      return "RequestTransferExit";
    case 0x38:
      return "RequestFileTransfer";
    case 0x3D:
      return "WriteMemoryByAddress";
    case 0x3E:
      return "TesterPresent";
    case 0x83:
      return "AccessTimingParameter";
    case 0x84:
      return "SecuredDataTransmission";
    case 0x85:
      return "ControlDTCSetting";
    case 0x86:
      return "ResponseOnEvent";
    case 0x87:
      return "LinkControl";
    default:
      return "Service";
  }
}

// Имена стандартных DID (в т.ч. используемых Geely/Volvo).
static inline const char *did_name(uint16_t did) {
  switch (did) {
    case 0xF186:
      return "ActiveSession";
    case 0xF187:
      return "PartNumber";
    case 0xF188:
      return "ECUSoftwareNumber";
    case 0xF189:
      return "ECUSoftwareVersion";
    case 0xF18A:
      return "SupplierIdentifier";
    case 0xF18C:
      return "ECUSerialNumber";
    case 0xF190:
      return "VIN";
    case 0xF191:
      return "ECUHardwareNumber";
    case 0xF192:
      return "SupplierHardwareNumber";
    case 0xF194:
      return "SupplierSWNumber";
    case 0xF195:
      return "SupplierSWVersion";
    case 0xF197:
      return "ECUHardwareVersion";
    case 0xF198:
      return "SupplierHardwareVersion";
    case 0xF19D:
      return "ECUInstallDate";
    case 0xF19E:
      return "ODXFile";
    case 0xF1A0:
      return "ECUConfiguration";
    case 0xF1A1:
      return "ECUConfiguration2";
    default:
      return nullptr;
  }
}

static inline const char *nrc_name(uint8_t nrc) {
  switch (nrc) {
    case 0x10:
      return "generalReject";
    case 0x11:
      return "serviceNotSupported";
    case 0x12:
      return "subFunctionNotSupported";
    case 0x13:
      return "incorrectMessageLength";
    case 0x14:
      return "responseTooLong";
    case 0x21:
      return "busyRepeatRequest";
    case 0x22:
      return "conditionsNotCorrect";
    case 0x24:
      return "requestSequenceError";
    case 0x25:
      return "noResponseFromSubnet";
    case 0x26:
      return "failurePreventsExecution";
    case 0x31:
      return "requestOutOfRange";
    case 0x33:
      return "securityAccessDenied";
    case 0x35:
      return "invalidKey";
    case 0x36:
      return "exceedNumberOfAttempts";
    case 0x37:
      return "requiredTimeDelayNotExpired";
    case 0x70:
      return "uploadDownloadNotAccepted";
    case 0x71:
      return "transferDataSuspended";
    case 0x72:
      return "generalProgrammingFailure";
    case 0x73:
      return "wrongBlockSequenceCounter";
    case 0x78:
      return "responsePending";
    case 0x7E:
      return "subFunctionNotSupportedInSession";
    case 0x7F:
      return "serviceNotSupportedInSession";
    default:
      return "unknownNRC";
  }
}

// Формат DTC: 3 байта (2 байта код + 1 байт FailureType).
static inline void format_dtc(const uint8_t *dtc, char *out, size_t out_n) {
  static const char letters[4] = {'P', 'C', 'B', 'U'};
  std::snprintf(out, out_n, "%c%X%X%X%X-%02X", letters[(dtc[0] >> 6) & 0x03], (dtc[0] >> 4) & 0x03, dtc[0] & 0x0F,
                (dtc[1] >> 4) & 0x0F, dtc[1] & 0x0F, dtc[2]);
}

// ---------------------------- декодер --------------------------------------

struct Stats {
  uint32_t frames{0};   // принято диагностических CAN-кадров
  uint32_t packets{0};  // собрано и расшифровано UDS-сообщений
  uint32_t errors{0};   // ошибки ISO-TP / формата кадра
  uint32_t nrc{0};      // получено отрицательных ответов (0x7F)
};

struct TpSession {
  uint32_t can_id{0};
  bool active{false};
  uint16_t expected{0};
  uint16_t received{0};
  uint8_t next_sn{1};
  uint32_t last_ms{0};
  uint8_t buffer[TP_MAX_MESSAGE]{};
};

class Reader {
 public:
  void begin() { ESP_LOGI(TAG, "UDS decoder: passive listen mode, ISO-TP ready"); }

  // Вызывать из canbus on_frame для каждого диагностического кадра.
  void handle_frame(uint32_t can_id, const uint8_t *data, size_t len);

  // Периодический вызов (раз в секунду): ловит таймауты незавершённых сообщений.
  void poll();

  uint32_t frames() const { return this->stats_.frames; }
  uint32_t packets() const { return this->stats_.packets; }
  uint32_t errors() const { return this->stats_.errors; }
  uint32_t nrc() const { return this->stats_.nrc; }
  const std::string &last() const { return this->last_; }

 protected:
  TpSession *session_(uint32_t can_id);
  void reset_(TpSession &session);
  void decode_(uint32_t can_id, const uint8_t *data, size_t len);
  void error_(uint32_t can_id, const char *what);

  void format_request_(uint32_t can_id, const uint8_t *data, size_t len, char *out, size_t out_n);
  void format_response_(uint32_t can_id, const uint8_t *data, size_t len, char *out, size_t out_n);

  Stats stats_{};
  TpSession sessions_[TP_MAX_SESSIONS]{};
  std::string last_{"-"};
};

// Ответы ЭБУ: 11-бит 0x7E8..0x7EF, 29-бит 0x18DAF1xx.
static inline bool is_response_id(uint32_t can_id) {
  return (can_id & 0x7F8) == 0x7E8 || (can_id >= 0x18DAF100 && can_id <= 0x18DAF1FF);
}

inline TpSession *Reader::session_(uint32_t can_id) {
  TpSession *free_slot = nullptr;
  for (auto &session : this->sessions_) {
    if (session.active && session.can_id == can_id)
      return &session;
    if (!session.active && free_slot == nullptr)
      free_slot = &session;
  }
  if (free_slot != nullptr) {
    free_slot->can_id = can_id;
    return free_slot;
  }
  // Все слоты заняты - забираем самый старый (оборванное сообщение).
  TpSession *oldest = &this->sessions_[0];
  for (auto &session : this->sessions_) {
    if (session.last_ms < oldest->last_ms)
      oldest = &session;
  }
  this->error_(oldest->can_id, "нет свободной ISO-TP сессии");
  this->reset_(*oldest);
  oldest->can_id = can_id;
  return oldest;
}

inline void Reader::reset_(TpSession &session) {
  session.active = false;
  session.expected = 0;
  session.received = 0;
  session.next_sn = 1;
}

inline void Reader::error_(uint32_t can_id, const char *what) {
  this->stats_.errors++;
  ESP_LOGW(TAG, "0x%03X: %s", static_cast<unsigned>(can_id), what);
}

inline void Reader::handle_frame(uint32_t can_id, const uint8_t *data, size_t len) {
  this->stats_.frames++;
  if (data == nullptr || len == 0) {
    this->error_(can_id, "пустой кадр");
    return;
  }

  const uint8_t pci = data[0] >> 4;

  switch (pci) {
    case 0x0: {  // Single Frame
      size_t offset = 1;
      size_t payload_len = data[0] & 0x0F;
      if (payload_len == 0) {  // escape-последовательность ISO 15765-2:2016
        if (len < 2) {
          this->error_(can_id, "SF: отсутствует длина");
          return;
        }
        payload_len = data[1];
        offset = 2;
      }
      if (offset + payload_len > len) {
        this->error_(can_id, "SF: длина больше кадра");
        return;
      }
      this->decode_(can_id, data + offset, payload_len);
      break;
    }

    case 0x1: {  // First Frame (начало многокадрового сообщения)
      size_t total = (static_cast<size_t>(data[0] & 0x0F) << 8) | data[1];
      if (total == 0 || total > TP_MAX_MESSAGE) {
        this->error_(can_id, "FF: недопустимая длина");
        return;
      }
      if (len < 3) {
        this->error_(can_id, "FF: короткий кадр");
        return;
      }
      TpSession *session = this->session_(can_id);
      this->reset_(*session);
      session->active = true;
      session->expected = static_cast<uint16_t>(total);
      session->received = static_cast<uint16_t>(std::min<size_t>(len - 2, total));
      std::memcpy(session->buffer, data + 2, session->received);
      session->next_sn = 1;
      session->last_ms = esphome::millis();
      ESP_LOGV(TAG, "0x%03X FF: ожидается %u байт", can_id, static_cast<unsigned>(total));
      if (session->received >= session->expected) {
        this->decode_(can_id, session->buffer, session->received);
        this->reset_(*session);
      }
      break;
    }

    case 0x2: {  // Consecutive Frame
      TpSession *session = this->session_(can_id);
      if (!session->active) {
        this->error_(can_id, "CF без первого кадра");
        return;
      }
      if (esphome::millis() - session->last_ms > TP_TIMEOUT_MS) {
        this->error_(can_id, "таймаут ISO-TP");
        this->reset_(*session);
        return;
      }
      uint8_t sn = data[0] & 0x0F;
      if (sn != session->next_sn) {
        this->error_(can_id, "CF: нарушен порядковый номер");
        this->reset_(*session);
        return;
      }
      size_t space = session->expected - session->received;
      size_t chunk = std::min<size_t>(space, len - 1);
      std::memcpy(session->buffer + session->received, data + 1, chunk);
      session->received += static_cast<uint16_t>(chunk);
      session->next_sn = (session->next_sn + 1) & 0x0F;
      session->last_ms = esphome::millis();
      if (session->received >= session->expected) {
        this->decode_(can_id, session->buffer, session->received);
        this->reset_(*session);
      }
      break;
    }

    case 0x3:  // Flow Control - отправляет приёмник, нам только логировать
      ESP_LOGV(TAG, "0x%03X FC (flow control)", can_id);
      break;

    default:
      this->error_(can_id, "неизвестный PCI");
      break;
  }
}

inline void Reader::poll() {
  uint32_t now = esphome::millis();
  for (auto &session : this->sessions_) {
    if (session.active && now - session.last_ms > TP_TIMEOUT_MS) {
      this->error_(session.can_id, "таймаут ISO-TP");
      this->reset_(session);
    }
  }
}

inline void Reader::decode_(uint32_t can_id, const uint8_t *data, size_t len) {
  if (len == 0) {
    this->error_(can_id, "пустое UDS-сообщение");
    return;
  }

  this->stats_.packets++;

  char text[192];
  if (len >= 3 && data[0] == 0x7F) {
    this->stats_.nrc++;
    std::snprintf(text, sizeof(text), "[0x%03X] NRC 0x%02X %s (сервис 0x%02X)", static_cast<unsigned>(can_id),
                  data[2], nrc_name(data[2]), data[1]);
  } else if (is_response_id(can_id)) {
    this->format_response_(can_id, data, len, text, sizeof(text));
  } else {
    this->format_request_(can_id, data, len, text, sizeof(text));
  }

  ESP_LOGI(TAG, "%s", text);
  this->last_ = text;
}

inline void Reader::format_request_(uint32_t can_id, const uint8_t *data, size_t len, char *out, size_t out_n) {
  size_t pos = std::snprintf(out, out_n, "[0x%03X] REQ %s", static_cast<unsigned>(can_id), service_name(data[0]));

  switch (data[0]) {
    case 0x10:
      if (len >= 2)
        append(out, out_n, pos, " %s(0x%02X)", session_name(data[1]), data[1]);
      break;
    case 0x11:
      if (len >= 2)
        append(out, out_n, pos, " type=0x%02X", data[1]);
      break;
    case 0x14:
      if (len >= 4)
        append(out, out_n, pos, " group=0x%02X%02X%02X", data[1], data[2], data[3]);
      break;
    case 0x19:
      if (len >= 2)
        append(out, out_n, pos, " sub=0x%02X", data[1]);
      break;
    case 0x09:
      if (len >= 2)
        append(out, out_n, pos, " PID=0x%02X", data[1]);
      break;
    case 0x22:
      for (size_t i = 1; i + 1 < len; i += 2) {
        const uint16_t did = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
        const char *name = did_name(did);
        if (name != nullptr)
          append(out, out_n, pos, " %s(0x%04X)", name, did);
        else
          append(out, out_n, pos, " 0x%04X", did);
      }
      break;
    case 0x27:
      if (len >= 2)
        append(out, out_n, pos, " %s", (data[1] & 1) ? "requestSeed" : "sendKey");
      break;
    case 0x28:
      if (len >= 3)
        append(out, out_n, pos, " control=0x%02X comm=0x%02X", data[1], data[2]);
      break;
    case 0x2E:
      if (len >= 3) {
        const uint16_t did = static_cast<uint16_t>((data[1] << 8) | data[2]);
        const char *name = did_name(did);
        if (name != nullptr)
          append(out, out_n, pos, " %s(0x%04X)=", name, did);
        else
          append(out, out_n, pos, " 0x%04X=", did);
        append_value(out, out_n, pos, data + 3, len - 3);
      }
      break;
    case 0x31:
      if (len >= 4)
        append(out, out_n, pos, " sub=0x%02X routine=0x%02X%02X", data[1], data[2], data[3]);
      break;
    case 0x3E:
      if (len >= 2)
        append(out, out_n, pos, " sub=0x%02X", data[1]);
      break;
    default:
      if (len > 1) {
        append(out, out_n, pos, " ");
        append_hex(out, out_n, pos, data + 1, len - 1);
      }
      break;
  }
}

inline void Reader::format_response_(uint32_t can_id, const uint8_t *data, size_t len, char *out, size_t out_n) {
  const uint8_t sid = data[0];

  if (sid < 0x40) {
    size_t pos = std::snprintf(out, out_n, "[0x%03X] RX ", static_cast<unsigned>(can_id));
    append_hex(out, out_n, pos, data, len);
    return;
  }

  const uint8_t service = sid - 0x40;
  size_t pos = std::snprintf(out, out_n, "[0x%03X] RSP %s", static_cast<unsigned>(can_id), service_name(service));

  switch (service) {
    case 0x09:
      if (len >= 4 && data[1] == 0x02) {
        // OBD Mode 09 PID 02: 49 02 01 <VIN, 17 ASCII>
        append(out, out_n, pos, " VIN=");
        append_value(out, out_n, pos, data + 3, std::min<size_t>(len - 3, 17));
      } else if (len >= 2) {
        append(out, out_n, pos, " PID=0x%02X ", data[1]);
        append_hex(out, out_n, pos, data + 2, len - 2);
      }
      break;

    case 0x10:
      if (len >= 2)
        append(out, out_n, pos, " %s(0x%02X)", session_name(data[1]), data[1]);
      if (len >= 6)
        append(out, out_n, pos, " P2=%ums", static_cast<unsigned>((data[2] << 8) | data[3]));
      break;

    case 0x11:
      if (len >= 2)
        append(out, out_n, pos, " resetType=0x%02X", data[1]);
      break;

    case 0x19: {
      if (len < 2)
        break;
      const uint8_t sub = data[1];
      append(out, out_n, pos, " sub=0x%02X", sub);
      if ((sub == 0x02 || sub == 0x04 || sub == 0x06) && len >= 3) {
        // 59 xx <availabilityMask> <DTC[4] x N>
        size_t count = (len - 3) / 4;
        append(out, out_n, pos, " DTC=%u:", static_cast<unsigned>(count));
        char dtc[16];
        for (size_t i = 0; i < count; i++) {
          const uint8_t *record = data + 3 + i * 4;
          format_dtc(record, dtc, sizeof(dtc));
          append(out, out_n, pos, " %s[%02X]", dtc, record[3]);
        }
      } else if ((sub == 0x01 || sub == 0x03) && len >= 6) {
        append(out, out_n, pos, " count=%u", static_cast<unsigned>((data[4] << 8) | data[5]));
      } else if (len > 2) {
        append(out, out_n, pos, " ");
        append_hex(out, out_n, pos, data + 2, len - 2);
      }
      break;
    }

    case 0x22: {
      if (len < 3)
        break;
      const uint16_t did = static_cast<uint16_t>((data[1] << 8) | data[2]);
      const char *name = did_name(did);
      if (name != nullptr)
        append(out, out_n, pos, " %s(0x%04X)=", name, did);
      else
        append(out, out_n, pos, " 0x%04X=", did);
      append_value(out, out_n, pos, data + 3, len - 3);
      break;
    }

    case 0x27:
      if (len >= 2) {
        append(out, out_n, pos, " %s", (data[1] & 1) ? "seed" : "key");
        if (len > 2) {
          append(out, out_n, pos, "=");
          append_hex(out, out_n, pos, data + 2, len - 2, 8);
        }
      }
      break;

    case 0x28:
      if (len >= 2)
        append(out, out_n, pos, " control=0x%02X", data[1]);
      break;

    case 0x2E:
      if (len >= 3) {
        const uint16_t did = static_cast<uint16_t>((data[1] << 8) | data[2]);
        append(out, out_n, pos, " 0x%04X", did);
      }
      break;

    case 0x31:
      if (len >= 4) {
        append(out, out_n, pos, " sub=0x%02X routine=0x%02X%02X", data[1], data[2], data[3]);
        if (len >= 5)
          append(out, out_n, pos, " status=0x%02X", data[4]);
      }
      break;

    case 0x3E:
      if (len >= 2)
        append(out, out_n, pos, " sub=0x%02X", data[1]);
      break;

    default:
      if (len > 1) {
        append(out, out_n, pos, " ");
        append_hex(out, out_n, pos, data + 1, len - 1);
      }
      break;
  }
}

// Единственный экземпляр декодера (function-local static - без ODR-проблем).
inline Reader &reader() {
  static Reader instance;
  return instance;
}

// ------------------------ монитор всей CAN-шины -----------------------------
//
// Считает все кадры, ведёт таблицу по каждому ID (количество, период,
// последние данные) и решает, какие ID отдать UDS-декодеру.

// Диагностические ID, которые имеет смысл разбирать как UDS/ISO-TP.
static inline bool is_uds_id(uint32_t can_id) {
  if (can_id == 0x7DF || can_id == 0x18DB33F1)  // функциональные запросы
    return true;
  if ((can_id & 0x7F8) == 0x7E0 || (can_id & 0x7F8) == 0x7E8)  // 11-бит
    return true;
  if ((can_id >> 16) == 0x18DA)  // 29-бит: 0x18DA<target><tester>
    return true;
  return false;
}

struct IdStat {
  uint32_t can_id{0};
  uint32_t count{0};
  uint32_t last_ms{0};
  uint32_t period_ms{0};
  uint32_t last_log_ms{0};
  bool used{false};
  bool extended{false};
  bool rtr{false};
  uint8_t len{0};
  uint8_t data[8]{};
};

class Monitor {
 public:
  // log_every_frame: печатать каждый кадр (осторожно, на 500 кбит/с это flood)
  // throttle_ms:     не чаще одного лога на ID за период (0 = только новые ID)
  // dump_ms:         период сводной таблицы ID (0 = не печатать)
  void begin(bool log_every_frame, uint32_t throttle_ms, uint32_t dump_ms);

  void on_frame(uint32_t can_id, bool extended, bool rtr, const uint8_t *data, size_t len);
  void poll();

  uint32_t unique_ids() const;
  uint32_t fps() const { return this->fps_; }
  uint32_t frames() const { return this->frames_; }

 protected:
  IdStat *slot_(uint32_t can_id, bool extended);
  static void reset_(IdStat &entry, uint32_t can_id, bool extended);
  void log_frame_(const IdStat &entry);
  void dump_();

  IdStat ids_[MON_MAX_IDS]{};
  bool log_every_frame_{false};
  uint32_t throttle_ms_{1000};
  uint32_t dump_ms_{10000};
  uint32_t last_dump_ms_{0};
  uint32_t frames_{0};
  uint32_t frames_second_{0};
  uint32_t fps_{0};
};

inline void Monitor::begin(bool log_every_frame, uint32_t throttle_ms, uint32_t dump_ms) {
  this->log_every_frame_ = log_every_frame;
  this->throttle_ms_ = throttle_ms;
  this->dump_ms_ = dump_ms;
  this->last_dump_ms_ = esphome::millis();
  ESP_LOGI(TAG_BUS, "монитор шины: %s, throttle=%u ms, таблица=%u ms",
           log_every_frame ? "каждый кадр" : "не чаще 1 раза на ID за период",
           static_cast<unsigned>(throttle_ms), static_cast<unsigned>(dump_ms));
}

inline void Monitor::reset_(IdStat &entry, uint32_t can_id, bool extended) {
  entry.can_id = can_id;
  entry.extended = extended;
  entry.count = 0;
  entry.last_ms = 0;
  entry.period_ms = 0;
  entry.last_log_ms = 0;
  entry.rtr = false;
  entry.len = 0;
  entry.used = true;
  std::memset(entry.data, 0, sizeof(entry.data));
}

inline IdStat *Monitor::slot_(uint32_t can_id, bool extended) {
  IdStat *free_slot = nullptr;
  IdStat *oldest = nullptr;
  for (auto &entry : this->ids_) {
    if (entry.used && entry.can_id == can_id && entry.extended == extended)
      return &entry;
    if (!entry.used && free_slot == nullptr)
      free_slot = &entry;
    if (entry.used && (oldest == nullptr || entry.last_ms < oldest->last_ms))
      oldest = &entry;
  }
  if (free_slot != nullptr) {
    reset_(*free_slot, can_id, extended);
    return free_slot;
  }
  // Таблица заполнена - вытесняем самый давний ID.
  reset_(*oldest, can_id, extended);
  return oldest;
}

inline void Monitor::on_frame(uint32_t can_id, bool extended, bool rtr, const uint8_t *data, size_t len) {
  this->frames_++;
  this->frames_second_++;

  const uint32_t now = esphome::millis();
  IdStat *entry = this->slot_(can_id, extended);
  const bool is_new = entry->count == 0;

  if (entry->last_ms != 0) {
    const uint32_t delta = now - entry->last_ms;
    entry->period_ms = entry->period_ms == 0 ? delta : (entry->period_ms * 3 + delta) / 4;
  }
  entry->last_ms = now;
  entry->count++;
  entry->rtr = rtr;
  entry->len = static_cast<uint8_t>(std::min<size_t>(len, 8));
  if (entry->len > 0 && data != nullptr)
    std::memcpy(entry->data, data, entry->len);

  const bool should_log =
      this->log_every_frame_ || (this->throttle_ms_ > 0 && now - entry->last_log_ms >= this->throttle_ms_);
  if (is_new || should_log) {
    if (is_new) {
      char idbuf[24];
      size_t pos = 0;
      append(idbuf, sizeof(idbuf), pos, extended ? "0x%08X" : "0x%03X", static_cast<unsigned>(can_id));
      ESP_LOGI(TAG_BUS, "новый ID %s%s (DLC %u)", extended ? "EXT " : "STD ", idbuf, entry->len);
    }
    this->log_frame_(*entry);
    entry->last_log_ms = now;
  }

  if (!rtr && is_uds_id(can_id))
    reader().handle_frame(can_id, data, len);
}

inline void Monitor::log_frame_(const IdStat &entry) {
  char buf[96];
  size_t pos = 0;
  append(buf, sizeof(buf), pos, entry.extended ? "EXT 0x%08X" : "STD 0x%03X",
         static_cast<unsigned>(entry.can_id));
  append(buf, sizeof(buf), pos, " [%u]", entry.len);
  if (entry.rtr)
    append(buf, sizeof(buf), pos, " RTR");
  else if (entry.len > 0) {
    append(buf, sizeof(buf), pos, " ");
    append_hex(buf, sizeof(buf), pos, entry.data, entry.len, 8);
  }
  ESP_LOGD(TAG_BUS, "%s", buf);
}

inline void Monitor::poll() {
  this->fps_ = this->frames_second_;
  this->frames_second_ = 0;

  const uint32_t now = esphome::millis();
  if (this->dump_ms_ > 0 && now - this->last_dump_ms_ >= this->dump_ms_) {
    this->last_dump_ms_ = now;
    this->dump_();
  }
}

inline uint32_t Monitor::unique_ids() const {
  const uint32_t now = esphome::millis();
  uint32_t count = 0;
  for (const auto &entry : this->ids_) {
    if (entry.used && entry.count > 0 && now - entry.last_ms <= MON_ID_EXPIRE_MS)
      count++;
  }
  return count;
}

inline void Monitor::dump_() {
  IdStat *list[MON_MAX_IDS];
  size_t n = 0;
  const uint32_t now = esphome::millis();
  for (auto &entry : this->ids_) {
    if (entry.used && entry.count > 0 && now - entry.last_ms <= MON_ID_EXPIRE_MS)
      list[n++] = &entry;
  }

  // Сортировка по ID (вставками - элементов немного, раз в несколько секунд).
  for (size_t i = 1; i < n; i++) {
    IdStat *key = list[i];
    size_t j = i;
    while (j > 0 && list[j - 1]->can_id > key->can_id) {
      list[j] = list[j - 1];
      j--;
    }
    list[j] = key;
  }

  ESP_LOGI(TAG_BUS, "--- шина: ID=%u, FPS=%u, всего кадров=%u ---", static_cast<unsigned>(n),
           static_cast<unsigned>(this->fps_), static_cast<unsigned>(this->frames_));
  for (size_t i = 0; i < n; i++) {
    const IdStat *entry = list[i];
    char buf[112];
    size_t pos = 0;
    append(buf, sizeof(buf), pos, entry->extended ? "EXT 0x%08X" : "STD 0x%03X",
           static_cast<unsigned>(entry->can_id));
    append(buf, sizeof(buf), pos, " cnt=%7u T=%5ums [%u] ", static_cast<unsigned>(entry->count),
           static_cast<unsigned>(entry->period_ms), entry->len);
    if (entry->rtr)
      append(buf, sizeof(buf), pos, "RTR");
    else
      append_hex(buf, sizeof(buf), pos, entry->data, entry->len, 8);
    ESP_LOGI(TAG_BUS, "%s", buf);
  }
}

// Единственный экземпляр монитора.
inline Monitor &monitor() {
  static Monitor instance;
  return instance;
}

}  // namespace uds_decode
