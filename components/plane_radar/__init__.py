import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_LATITUDE, CONF_LONGITUDE, CONF_TRIGGER_ID

DEPENDENCIES = ["lvgl", "network"]
CODEOWNERS   = ["@sendspin"]

plane_radar_ns = cg.esphome_ns.namespace("plane_radar")
PlaneRadar     = plane_radar_ns.class_("PlaneRadar", cg.Component)

PlaneRadarAircraftUpdateTrigger = plane_radar_ns.class_(
    "PlaneRadarAircraftUpdateTrigger", automation.Trigger.template(cg.int_)
)
PlaneRadarStartAction      = plane_radar_ns.class_("PlaneRadarStartAction",      automation.Action)
PlaneRadarStopAction       = plane_radar_ns.class_("PlaneRadarStopAction",       automation.Action)
PlaneRadarToggleAction     = plane_radar_ns.class_("PlaneRadarToggleAction",     automation.Action)
PlaneRadarCycleRangeAction = plane_radar_ns.class_("PlaneRadarCycleRangeAction", automation.Action)

CONF_UPDATE_INTERVAL    = "update_interval"
CONF_INITIAL_RANGE_KM   = "initial_range_km"
CONF_USE_MILES          = "use_miles"
CONF_ON_AIRCRAFT_UPDATE = "on_aircraft_update"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID():                                               cv.declare_id(PlaneRadar),
    cv.Required(CONF_LATITUDE):                                    cv.float_,
    cv.Required(CONF_LONGITUDE):                                   cv.float_,
    cv.Optional(CONF_UPDATE_INTERVAL,  default="15s"):             cv.positive_time_period_milliseconds,
    cv.Optional(CONF_INITIAL_RANGE_KM, default=50):                cv.int_range(min=5, max=500),
    cv.Optional(CONF_USE_MILES,        default=False):             cv.boolean,
    cv.Optional(CONF_ON_AIRCRAFT_UPDATE): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PlaneRadarAircraftUpdateTrigger)}
    ),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_latitude(config[CONF_LATITUDE]))
    cg.add(var.set_longitude(config[CONF_LONGITUDE]))
    cg.add(var.set_initial_range_km(config[CONF_INITIAL_RANGE_KM]))
    cg.add(var.set_use_miles(config[CONF_USE_MILES]))
    for conf in config.get(CONF_ON_AIRCRAFT_UPDATE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        await automation.build_automation(trigger, [(cg.int_, "count")], conf)
        cg.add(var.register_aircraft_update_trigger(trigger))


_ACTION_SCHEMA = cv.Schema({cv.GenerateID(CONF_ID): cv.use_id(PlaneRadar)})


@automation.register_action("plane_radar.start", PlaneRadarStartAction, _ACTION_SCHEMA, synchronous=True)
async def _start_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("plane_radar.stop", PlaneRadarStopAction, _ACTION_SCHEMA, synchronous=True)
async def _stop_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("plane_radar.toggle", PlaneRadarToggleAction, _ACTION_SCHEMA, synchronous=True)
async def _toggle_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("plane_radar.cycle_range", PlaneRadarCycleRangeAction, _ACTION_SCHEMA, synchronous=True)
async def _cycle_range_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
