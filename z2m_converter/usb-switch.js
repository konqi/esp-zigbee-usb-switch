const {
  identify,
  onOff,
} = require("zigbee-herdsman-converters/lib/modernExtend");
const exposes = require("zigbee-herdsman-converters/lib/exposes");
const reporting = require("zigbee-herdsman-converters/lib/reporting");
const utils = require("zigbee-herdsman-converters/lib/utils");
const e = exposes.presets;
const ea = exposes.access;

const channelValues = ["ch_1", "ch_2"];

const switchLocalInput = {
  cluster: "genMultistateValue",
  type: ["readResponse", "attributeReport"],
  convert: (model, msg, publish, options, meta) => {
    const presentValue = msg.data["presentValue"];
    const action = channelValues[presentValue];
    const property = "channel";
    return {
      [property]: `${action}`,
    };
  },
};

const switchLocalOutput = {
  key: ["channel"],
  convertSet: async (entity, key, value, meta) => {
    utils.assertString(value, key);
    await entity.write(
      "genMultistateValue",
      { presentValue: channelValues.indexOf(value) },
      utils.getOptions(meta.mapped, entity)
    );
    return { state: { channel: value } };
  },
  convertGet: async (entity, key, meta) => {
    await entity.read("genMultistateValue", ["presentValue"]);
  },
};

const bind = async (endpoint, target, clusters) => {
  for (const cluster of clusters) {
    await endpoint.bind(cluster, target);
  }
};

const definition = {
  zigbeeModel: ["zigbee-usb-switch"],
  model: "zigbee-usb-switch",
  vendor: "KONQI",
  description: "konqi's homebrew usb-switch extension",
  fromZigbee: [switchLocalInput],
  toZigbee: [switchLocalOutput],
  exposes: [e.enum("channel", ea.ALL, channelValues)],
  extend: [identify(), onOff({ powerOnBehavior: false })],
  configure: async (device, coordinatorEndpoint, logger) => {
    const endpoint = device.getEndpoint(10);
    // await endpoint.read("genMultistateValue", ["presentValue"]);
    await endpoint.bind("genMultistateValue", coordinatorEndpoint);
    await endpoint.configureReporting("genMultistateValue", {
      attribute: "presentValue",
      minimumReportInterval: 0,
      maximumReportInterval: 3600,
      reportableChange: 1,
    });
  },
  meta: {},
};

module.exports = definition;
