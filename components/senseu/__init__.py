import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client
from esphome.const import CONF_ID, CONF_UPDATE_INTERVAL

CODEOWNERS = ["@andyboeh"]
DEPENDENCIES = ["ble_client", "binary_sensor", "text_sensor", "sensor"]
MULTI_CONF = True

CONF_BABY_CODE = "baby_code"
CONF_PRESERVE_MONITORING_SETTINGS = "preserve_monitoring_settings"
CONF_PRO_MODE = "pro_mode"
CONF_DATA_TIMEOUT = "data_timeout"
CONF_CONNECTION_TIMEOUT = "connection_timeout"

senseu_ns = cg.esphome_ns.namespace("senseu")
senseu = senseu_ns.class_("SenseU", ble_client.BLEClientNode, cg.Component)


def validate_baby_code(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"[0-9a-fA-F]{12}", value):
        raise cv.Invalid("baby_code must be exactly 12 hexadecimal characters (quote it)")
    return value


def validate_timeouts(config):
    if config[CONF_DATA_TIMEOUT] <= config[CONF_UPDATE_INTERVAL]:
        raise cv.Invalid("data_timeout must be greater than update_interval")
    # Pro defaults to preserving settings. Legacy configurations keep their default.
    config.setdefault(CONF_PRESERVE_MONITORING_SETTINGS, config[CONF_PRO_MODE])
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(senseu),
        cv.Optional(CONF_BABY_CODE): validate_baby_code,
        cv.Optional(CONF_PRO_MODE, default=False): cv.boolean,
        cv.Optional(CONF_PRESERVE_MONITORING_SETTINGS): cv.boolean,
        cv.Optional(CONF_UPDATE_INTERVAL, default="2s"): cv.All(
            cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=1000))),
        cv.Optional(CONF_DATA_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_CONNECTION_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
    }).extend(ble_client.BLE_CLIENT_SCHEMA).extend(cv.COMPONENT_SCHEMA),
    validate_timeouts,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_pro_mode(config[CONF_PRO_MODE]))
    cg.add(var.set_preserve_monitoring_settings(config[CONF_PRESERVE_MONITORING_SETTINGS]))
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(var.set_data_timeout(config[CONF_DATA_TIMEOUT]))
    cg.add(var.set_connection_timeout(config[CONF_CONNECTION_TIMEOUT]))
    if CONF_BABY_CODE in config:
        cg.add(var.set_baby_code(config[CONF_BABY_CODE]))
