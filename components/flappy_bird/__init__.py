import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@sendspin"]

flappy_bird_ns = cg.esphome_ns.namespace("flappy_bird")
FlappyBirdGame = flappy_bird_ns.class_("FlappyBirdGame", cg.Component)

FlappyBirdStartAction  = flappy_bird_ns.class_("FlappyBirdStartAction",  automation.Action)
FlappyBirdStopAction   = flappy_bird_ns.class_("FlappyBirdStopAction",   automation.Action)
FlappyBirdToggleAction = flappy_bird_ns.class_("FlappyBirdToggleAction", automation.Action)
FlappyBirdFlapAction   = flappy_bird_ns.class_("FlappyBirdFlapAction",   automation.Action)

FlappyBirdGameOverTrigger = flappy_bird_ns.class_(
    "FlappyBirdGameOverTrigger", automation.Trigger.template(cg.int_)
)

CONF_GRAVITY       = "gravity"
CONF_FLAP_STRENGTH = "flap_strength"
CONF_PIPE_SPEED    = "pipe_speed"
CONF_GAP_SIZE      = "gap_size"
CONF_ON_GAME_OVER  = "on_game_over"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID():                                   cv.declare_id(FlappyBirdGame),
    cv.Optional(CONF_GRAVITY,       default=1.0):      cv.float_,
    cv.Optional(CONF_FLAP_STRENGTH, default=-9.5):     cv.float_,
    cv.Optional(CONF_PIPE_SPEED,    default=6.0):      cv.float_,
    cv.Optional(CONF_GAP_SIZE,      default=72):       cv.int_,
    cv.Optional(CONF_ON_GAME_OVER): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FlappyBirdGameOverTrigger)}
    ),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_gravity(config[CONF_GRAVITY]))
    cg.add(var.set_flap_strength(config[CONF_FLAP_STRENGTH]))
    cg.add(var.set_pipe_speed(config[CONF_PIPE_SPEED]))
    cg.add(var.set_gap_size(config[CONF_GAP_SIZE]))

    for conf in config.get(CONF_ON_GAME_OVER, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        await automation.build_automation(trigger, [(cg.int_, "score")], conf)
        cg.add(var.register_game_over_trigger(trigger))


_ACTION_SCHEMA = cv.Schema({cv.GenerateID(CONF_ID): cv.use_id(FlappyBirdGame)})


@automation.register_action("flappy_bird.start", FlappyBirdStartAction, _ACTION_SCHEMA, synchronous=True)
async def _start_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("flappy_bird.stop", FlappyBirdStopAction, _ACTION_SCHEMA, synchronous=True)
async def _stop_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("flappy_bird.toggle", FlappyBirdToggleAction, _ACTION_SCHEMA, synchronous=True)
async def _toggle_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("flappy_bird.flap", FlappyBirdFlapAction, _ACTION_SCHEMA, synchronous=True)
async def _flap_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
