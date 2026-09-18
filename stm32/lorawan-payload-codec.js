// LoRaBLE Remote: Milesight gateways and The Things Stack / TTN.
function Decode(fPort, bytes) {
  var decoded = {};
  var resultText = {
    0: "nog_niet_uitgevoerd",
    1: "geslaagd",
    2: "instellingen_ontbreken",
    3: "ble_stack_of_at_firmware_start_mislukt",
    4: "ble_apparaat_niet_gevonden",
    5: "verbinden_mislukt",
    6: "gatt_niet_gevonden",
    7: "eerste_uitlezing_mislukt",
    8: "schrijven_mislukt",
    9: "terugleescontrole_mislukt",
    10: "ble_beveiliging_mislukt",
    11: "ble_companion_bezet",
    12: "esp_companion_protocol_of_link_timeout"
  };
  var companionText = {
    0: "uit",
    1: "opstarten",
    2: "wifi_portaal",
    3: "ble_actie",
    4: "storing"
  };

  // Assign this codec to LoRaBLE devices only. The application port is configurable.
  if (fPort < 1 || fPort > 223 || fPort !== Math.floor(fPort)) {
    decoded.error = "onverwachte_fport";
    decoded.fPort = fPort;
    return decoded;
  }
  if (!bytes || bytes.length < 8) {
    decoded.error = "statuspayload_te_kort";
    return decoded;
  }
  decoded.fPort = fPort;

  // Bytes 0..7 zijn exact achterwaarts compatibel met firmware v3.1.
  decoded.firmware = bytes[0] + "." + bytes[1];
  decoded.contact_active = bytes[2] === 1;
  decoded.ble_result_code = bytes[3];
  decoded.ble_result = resultText[bytes[3]] || "onbekend";
  decoded.ble_attempts = bytes[4];
  decoded.load_value = bytes[5];
  decoded.load_value_hex = ("0" + bytes[5].toString(16)).slice(-2).toUpperCase();
  var loadMode = bytes[5] === 255 ? null : (bytes[5] & 15);
  decoded.load_control_mode = loadMode;
  decoded.load_control_mode_name = loadMode === null ? null :
    ['always_off','batterylife','conventional_1','conventional_2','always_on','user_defined_1','user_defined_2','aes'][loadMode] || 'unknown';
  decoded.load_always_on_verified = loadMode === 4 && bytes[3] === 1;
  decoded.load_always_off_verified = loadMode === 0 && bytes[3] === 1;
  decoded.load_note = 'Teruggelezen regelmodus; geen meting van de fysieke LOAD-uitgang.';
  decoded.command_pending = bytes[6] === 1;
  decoded.schema = bytes[7];

  if (bytes.length >= 12) {
    decoded.companion_state_code = bytes[8];
    decoded.companion_state = companionText[bytes[8]] || "onbekend";
    decoded.wifi_wanted = bytes[9] === 1;
    decoded.config_revision_low16 = bytes[10] | (bytes[11] << 8);
  }
  if (bytes.length >= 14) {
    var boardVbatMv = bytes[12] | (bytes[13] << 8);
    decoded.board_vbat_mv = boardVbatMv === 65535 ? null : boardVbatMv;
    decoded.board_vbat_v = boardVbatMv === 65535 ? null : boardVbatMv / 1000;
    decoded.board_vbat_note = "RAK battery/VBAT rail; niet de 12/24V op K1/K2";
  }
  if (bytes.length >= 18 && bytes[7] >= 3) {
    decoded.input_enabled = !!(bytes[14] & 1);
    decoded.relay_enabled = !!(bytes[14] & 2);
    decoded.relay_commanded_on = !!(bytes[14] & 4);
    decoded.relay_pulsing = !!(bytes[14] & 8);
    decoded.last_event = ['none','rising','falling','downlink','test_rising','test_falling','manual'][bytes[15]] || 'unknown';
    decoded.action_mask = bytes[16];
    decoded.ble_function_1_requested = !!(bytes[16] & 4);
    decoded.ble_function_2_requested = !!(bytes[16] & 8);
    decoded.requested_load_value = bytes[17] === 255 ? null : bytes[17];
    decoded.request_note = "Actieverzoek; geen bewijs van uitvoering. Generic GATT heeft geen LOAD-waarde.";
  }
  if(bytes.length>=20 && bytes[7]>=4) {
    decoded.device_profile=bytes[18];
    decoded.bluetooth_function=bytes[19]||null;
    decoded.ble_function_1_requested=bytes[19]===1;
    decoded.ble_function_2_requested=bytes[19]===2;
    if(bytes[18]!==1) {
      decoded.load_control_mode=null;decoded.load_control_mode_name=null;
      decoded.load_always_on_verified=false;decoded.load_always_off_verified=false;
    }
    if(bytes[18]===3) {
      decoded.batteryprotect_output_state=bytes[5]===255?null:bytes[5];
      decoded.batteryprotect_on_verified=bytes[5]===1&&bytes[3]===1;
      decoded.batteryprotect_off_verified=bytes[5]===0&&bytes[3]===1;
      decoded.load_note='BatteryProtect output-state register: 0 OFF, 1 ON; other values are not ON. Not an external voltage measurement.';
    }
  }
  return decoded;
}

// The Things Stack / TTN v3 formatter, shared with Milesight gateways.
function decodeUplink(input) {
  var data=Decode(input.fPort,input.bytes);
  return data.error?{errors:[data.error]}:{data:data};
}
