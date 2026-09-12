import esphome.codegen as cg
from esphome.components.esp32 import add_idf_component
import esphome.config_validation as cv
from esphome.const import CONF_BUFFER_SIZE, CONF_ID, CONF_TYPE, PLATFORM_ESP32
from esphome.types import ConfigType

CODEOWNERS = ["@DT-art1"]

AUTO_LOAD = ["camera"]

CONF_BUFFER_EXPAND_SIZE = "buffer_expand_size"
CONF_ENCODER_BUFFER_ID = "encoder_buffer_id"
CONF_QUALITY = "quality"

ESP32_CAMERA_ENCODER = "esp32_camera"

camera_ns = cg.esphome_ns.namespace("camera")
camera_encoder_ns = cg.esphome_ns.namespace("camera_encoder")

Encoder = camera_ns.class_("Encoder")
EncoderBufferImpl = camera_encoder_ns.class_("EncoderBufferImpl")

ESP32CameraJPEGEncoder = camera_encoder_ns.class_("ESP32CameraJPEGEncoder", Encoder)

MAX_JPEG_BUFFER_SIZE_2MB = 2 * 1024 * 1024

ESP32_CAMERA_ENCODER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ESP32CameraJPEGEncoder),
        cv.Optional(CONF_QUALITY, default=80): cv.int_range(1, 100),
        cv.Optional(CONF_BUFFER_SIZE, default=4096): cv.int_range(
            1024, MAX_JPEG_BUFFER_SIZE_2MB
        ),
        cv.Optional(CONF_BUFFER_EXPAND_SIZE, default=1024): cv.int_range(
            0, MAX_JPEG_BUFFER_SIZE_2MB
        ),
        cv.GenerateID(CONF_ENCODER_BUFFER_ID): cv.declare_id(EncoderBufferImpl),
    }
)

# ESP32 only, and by construction rather than by choice: the one encoder type
# this component has calls add_idf_component(), which needs ESP-IDF.  Without
# the gate it validates anywhere and then fails code generation with
#
#   KeyError: 'esp32'
#
# from CORE.data[KEY_ESP32][KEY_COMPONENTS], which tells a user nothing about
# the component being unavailable on their platform.
CONFIG_SCHEMA = cv.All(
    cv.typed_schema(
        {
            ESP32_CAMERA_ENCODER: ESP32_CAMERA_ENCODER_SCHEMA,
        },
        default_type=ESP32_CAMERA_ENCODER,
    ),
    cv.only_on([PLATFORM_ESP32]),
)


async def to_code(config: ConfigType) -> None:
    buffer = cg.new_Pvariable(config[CONF_ENCODER_BUFFER_ID])
    cg.add(buffer.set_buffer_size(config[CONF_BUFFER_SIZE]))
    if config[CONF_TYPE] == ESP32_CAMERA_ENCODER:
        add_idf_component(name="espressif/esp32-camera", ref="2.1.7")
        cg.add_define("USE_ESP32_CAMERA_JPEG_ENCODER")
        var = cg.new_Pvariable(
            config[CONF_ID],
            config[CONF_QUALITY],
            buffer,
        )
        cg.add(var.set_buffer_expand_size(config[CONF_BUFFER_EXPAND_SIZE]))
