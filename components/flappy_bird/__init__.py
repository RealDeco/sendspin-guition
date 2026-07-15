import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@sendspin"]

flappy_bird_ns = cg.esphome_ns.namespace("flappy_bird")
FlappyBirdGame = flappy_bird_ns.class_("FlappyBirdGame", cg.Component)

FlappyBirdStartAction  = flappy_bird_ns.class_("FlappyBirdStartAction",  automation.Action)
FlappyBirdStopAction   = flappy_bird_ns.class_("FlappyBirdStopAction",   automation.Action)
FlappyBirdToggleAction = flappy_bird_ns.class_("FlappyBirdToggleAction", automation.Action)
FlappyBirdFlapAction   = flappy_bird_ns.class_("FlappyBirdFlapAction",   automation.Action)

CONF_GRAVITY       = "gravity"
CONF_FLAP_STRENGTH = "flap_strength"
CONF_PIPE_SPEED    = "pipe_speed"
CONF_GAP_SIZE      = "gap_size"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID():                                   cv.declare_id(FlappyBirdGame),
    cv.Optional(CONF_GRAVITY,       default=0.45):     cv.float_,
    cv.Optional(CONF_FLAP_STRENGTH, default=-7.5):     cv.float_,
    cv.Optional(CONF_PIPE_SPEED,    default=2.5):      cv.float_,
    cv.Optional(CONF_GAP_SIZE,      default=72):       cv.int_,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_gravity(config[CONF_GRAVITY]))
    cg.add(var.set_flap_strength(config[CONF_FLAP_STRENGTH]))
    cg.add(var.set_pipe_speed(config[CONF_PIPE_SPEED]))
    cg.add(var.set_gap_size(config[CONF_GAP_SIZE]))


_ACTION_SCHEMA = cv.Schema({cv.GenerateID(CONF_ID): cv.use_id(FlappyBirdGame)})


@automation.register_action("flappy_bird.start", FlappyBirdStartAction, _ACTION_SCHEMA)
async def _start_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("flappy_bird.stop", FlappyBirdStopAction, _ACTION_SCHEMA)
async def _stop_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("flappy_bird.toggle", FlappyBirdToggleAction, _ACTION_SCHEMA)
async def _toggle_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("flappy_bird.flap", FlappyBirdFlapAction, _ACTION_SCHEMA)
async def _flap_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
