#include "preesu_rpc.h"

#include "mgos_rpc.h"

#define INPUT_NUMBER 4
#define OUTPUT_NUMBER 4

struct board_info {
    int input_pin[INPUT_NUMBER];
    int output_pin[OUTPUT_NUMBER];
    mgos_timer_id pulse_timer_id[OUTPUT_NUMBER];
} board = {.input_pin = {}, .output_pin = {}, .pulse_timer_id = {MGOS_INVALID_TIMER_ID}};

static void pulse_timer_cb(void* arg) {
    int output = (int)arg;
    mgos_gpio_write(board.output_pin[output], false);
    mgos_clear_timer(board.pulse_timer_id[output]);
    board.pulse_timer_id[output] = MGOS_INVALID_TIMER_ID;
    //TODO trigger event PULSE_FINISHED
    mgos_event_trigger(BOARD_PULSE_FINISHED, (int*)output);
}

static void rpc_handler_pulse_output(struct mg_rpc_request_info* ri, void* cb_arg,
                                     struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("Device.Pulse rpc called, payload: %.*s", args.len, args.p));
    int output, pulse_ms;
    if (json_scanf(args.p, args.len, ri->args_fmt, &output, &pulse_ms) != 2) {
        mg_rpc_send_errorf(ri, 400, "Output number and pulse millisecond are required");
        ri = NULL;
        return;
    }

    if (pulse_ms <= 0) {
        mg_rpc_send_errorf(ri, 400, "Pulse should be a positive number");
        ri = NULL;
        return;
    }

    if (output < 1 || output > 4) {
        mg_rpc_send_errorf(ri, 400, "Invalid output number");
        ri = NULL;
        return;
    }
    output--;

    mgos_gpio_write(board.output_pin[output], true);
    mgos_clear_timer(board.pulse_timer_id[output]);
    board.pulse_timer_id[output] = mgos_set_timer(pulse_ms, 0, pulse_timer_cb, (int*)output);
    mg_rpc_send_responsef(ri, "{output:%d, value: %d}", output, 1);
    mgos_event_trigger(BOARD_PULSE_STARTED, (int*)output);

    ri = NULL;
    (void)cb_arg;
    (void)fi;
}

static void rpc_handler_gpio_control(struct mg_rpc_request_info* ri, void* cb_arg,
                                     struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("Device.Output rpc called - payload %.*s", args.len, args.p));
    int output, action;  //-1: toggle, 0: low, 1: high
    if (json_scanf(args.p, args.len, ri->args_fmt, &output, &action) != 2) {
        mg_rpc_send_errorf(ri, 400, "Output number and action(-1: toggle, 0: low, 1: high) are required");
        ri = NULL;
        return;
    }

    if (output < 1 || output > 4) {
        mg_rpc_send_errorf(ri, 400, "Invalid output number");
        ri = NULL;
        return;
    }
    output--;

    mgos_clear_timer(board.pulse_timer_id[output]);
    board.pulse_timer_id[output] = MGOS_INVALID_TIMER_ID;
    int value = -1;
    switch (action) {
        case -1:
            value = mgos_gpio_toggle(board.output_pin[output]);
            break;
        case 0:
            mgos_gpio_write(board.output_pin[output], false);
            value = 0;
            break;
        case 1:
            mgos_gpio_write(board.output_pin[output], true);
            value = 1;
            break;
        default:
            mg_rpc_send_errorf(ri, 400, "Invalid action");
            ri = NULL;
            return;
    }
    mg_rpc_send_responsef(ri, "{output:%d, value: %d}", output, value);
    mgos_event_trigger(BOARD_OUTPUT_CHANGED, (int*)output);

    ri = NULL;
    (void)cb_arg;
    (void)fi;
}

static void rpc_handler_gpio_status(struct mg_rpc_request_info* ri, void* cb_arg,
                                    struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("Device.Input rpc called - payload %.*s", args.len, args.p));
    mg_rpc_send_responsef(ri,
                          "{input1:%d, input2: %d, input3:%d, input4: %d,"
                          "output1:%d, output2: %d, output3:%d, output4: %d}",
                          mgos_gpio_read(mgos_sys_config_get_board_input1()) ? 1 : -1,
                          mgos_gpio_read(mgos_sys_config_get_board_input2()) ? 1 : -1,
                          mgos_gpio_read(mgos_sys_config_get_board_input3()) ? 1 : -1,
                          mgos_gpio_read(mgos_sys_config_get_board_input4()) ? 1 : -1,
                          mgos_gpio_read_out(mgos_sys_config_get_board_output1()) ? 1 : -1,
                          mgos_gpio_read_out(mgos_sys_config_get_board_output2()) ? 1 : -1,
                          mgos_gpio_read_out(mgos_sys_config_get_board_output3()) ? 1 : -1,
                          mgos_gpio_read_out(mgos_sys_config_get_board_output4()) ? 1 : -1);
    mgos_event_trigger(BOARD_INPUT_STATE_REQUESTED, NULL);
    ri = NULL;
    (void)cb_arg;
    (void)fi;
}

static void rpc_handler_reset_config(struct mg_rpc_request_info* ri, void* cb_arg,
                                     struct mg_rpc_frame_info* fi, struct mg_str args) {
    int level;
    if (json_scanf(args.p, args.len, ri->args_fmt, &level) != 1) {
        mg_rpc_send_errorf(ri, 400, "Reset level is required");
        ri = NULL;
        return;
    }
    LOG(LL_INFO, ("Device.ResetConfig rpc called, level:%d", level));
    mgos_config_reset(level);
    mgos_system_restart_after(1000);
    mg_rpc_send_responsef(ri, "{status:%Q}", "success");
    ri = NULL;
    (void)cb_arg;
    (void)fi;
}

static int wifi_scan_result_printer(struct json_out* out, va_list* ap) {
    int len = 0;
    int num_res = va_arg(*ap, int);

    const struct mgos_wifi_scan_result* res =
        va_arg(*ap, const struct mgos_wifi_scan_result*);

    for (int i = 0; i < num_res; i++) {
        const struct mgos_wifi_scan_result* r = &res[i];
        if (i > 0)
            len += json_printf(out, ", ");

        len += json_printf(out,
                           "{ssid: %Q, bssid: \"%02x:%02x:%02x:%02x:%02x:%02x\", "
                           "auth: %d, channel: %d,"
                           " rssi: %d}",
                           r->ssid, r->bssid[0], r->bssid[1], r->bssid[2],
                           r->bssid[3], r->bssid[4], r->bssid[5], r->auth_mode,
                           r->channel, r->rssi);
    }

    return len;
}

static void wifi_scan_cb(int n, struct mgos_wifi_scan_result* res, void* arg) {
    struct mg_rpc_request_info* ri = (struct mg_rpc_request_info*)arg;
    if (n < 0) {
        mg_rpc_send_errorf(ri, n, "WIFI scan failed");
        return;
    }
    mg_rpc_send_responsef(ri, "[%M]", wifi_scan_result_printer, n, res);
}

static void rpc_handler_wifi_scan(struct mg_rpc_request_info* ri,
                                  void* cb_arg,
                                  struct mg_rpc_frame_info* fi,
                                  struct mg_str args) {
    LOG(LL_INFO, ("Device.WifiScan rpc called"));
    mgos_wifi_scan(wifi_scan_cb, ri);
    (void)args;
    (void)cb_arg;
    (void)fi;
}

static void rpc_handler_wifi_setup_sta(struct mg_rpc_request_info* ri,
                                       void* cb_arg,
                                       struct mg_rpc_frame_info* fi,
                                       struct mg_str args) {
    LOG(LL_INFO, ("Device.SetupSTA rpc called"));

    if (mgos_conf_parse_sub(mg_mk_str_n(args.p, args.len), mgos_config_schema_wifi_sta(), (void*)mgos_sys_config_get_wifi_sta()) &&
        mgos_sys_config_save_level(&mgos_sys_config, (enum mgos_config_level)MGOS_CONFIG_LEVEL_USER, false, NULL)) {
        LOG(LL_INFO, ("WIFI sta setup success"));
        mg_rpc_send_responsef(ri, "WIFI setup succesful");
        mgos_sys_config_set_wifi_ap_enable(false);
        mgos_system_restart_after(1000);
    } else {
        mg_rpc_send_errorf(ri, 400, "%s failed", "WIFI setup");
    }
    (void)fi;
    (void)cb_arg;
}

static void reconnect_gsm_timer_cb(void* arg) {
    mgos_event_trigger(BOARD_GSM_CONNECT, NULL);
    (void)arg;
}

static void rpc_handler_reset_gsm(struct mg_rpc_request_info* ri, void* cb_arg,
                                  struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("Device.ResetGSM rpc called"));

    mgos_event_trigger(BOARD_GSM_DISCONNECT, NULL);
    mgos_set_timer(5000, 0, reconnect_gsm_timer_cb, NULL);
    mg_rpc_send_responsef(ri, "{status:%Q}", "success");
    ri = NULL;
    (void)cb_arg;
    (void)fi;
}

static void rpc_handler_telemetry(struct mg_rpc_request_info* ri, void* cb_arg,
                                  struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("Device.Telemetry rpc called"));
    mgos_event_trigger(BOARD_TELEMETRY_REQUESTED, NULL);
    mg_rpc_send_responsef(ri, "{status:%Q}", "success");
    ri = NULL;
    (void)cb_arg;
    (void)fi;
}

static void rpc_handler_attribute(struct mg_rpc_request_info* ri, void* cb_arg,
                                  struct mg_rpc_frame_info* fi, struct mg_str args) {
    LOG(LL_INFO, ("Device.Attribute rpc called"));
    int type = 0;
    json_scanf(args.p, args.len, ri->args_fmt, &type);
    mgos_event_trigger(BOARD_ATTRIBUTE_REQUESTED, (int*)type);
    mg_rpc_send_responsef(ri, "{status:%Q}", "success");
    ri = NULL;
    (void)cb_arg;
    (void)fi;
}

bool mgos_preesu_board_init(void) {
    LOG(LL_DEBUG, ("Initializing preesu device"));
    if (!mgos_sys_config_get_board_enable()) {
        return true;
    }

    mgos_event_register_base(BOARD_EVENT_BASE, "Thingsboard Preesu Event");

    board.input_pin[0] = mgos_sys_config_get_board_input1();
    board.input_pin[1] = mgos_sys_config_get_board_input2();
    board.input_pin[2] = mgos_sys_config_get_board_input3();
    board.input_pin[3] = mgos_sys_config_get_board_input4();

    board.output_pin[0] = mgos_sys_config_get_board_output1();
    board.output_pin[1] = mgos_sys_config_get_board_output2();
    board.output_pin[2] = mgos_sys_config_get_board_output3();
    board.output_pin[3] = mgos_sys_config_get_board_output4();

    for (int i = 0; i < INPUT_NUMBER; i++) {
        mgos_gpio_setup_input(board.input_pin[i], MGOS_GPIO_PULL_UP);
        board.pulse_timer_id[i] = MGOS_INVALID_TIMER_ID;
    }

    for (int i = 0; i < OUTPUT_NUMBER; i++) {
        mgos_gpio_setup_input(board.input_pin[i], MGOS_GPIO_PULL_UP);
        mgos_gpio_setup_output(board.output_pin[i], false);
    }

    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.Pulse", "{output: %d, pulse_ms: %d}", rpc_handler_pulse_output, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.Output", "{output: %d, action: %d}", rpc_handler_gpio_control, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.Input", NULL, rpc_handler_gpio_status, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.ResetConfig", "{level: %d}", rpc_handler_reset_config, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.WifiScan", "", rpc_handler_wifi_scan, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.SetupSTA", "{enable: %B, ssid: %Q, pass: %Q}", rpc_handler_wifi_setup_sta, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.ResetGSM", NULL, rpc_handler_reset_gsm, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.Telemetry", NULL, rpc_handler_telemetry, NULL);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Device.Attribute", "{type: %d}", rpc_handler_attribute, NULL);
    return true;
}
