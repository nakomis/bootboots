"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
var __awaiter = (this && this.__awaiter) || function (thisArg, _arguments, P, generator) {
    function adopt(value) { return value instanceof P ? value : new P(function (resolve) { resolve(value); }); }
    return new (P || (P = Promise))(function (resolve, reject) {
        function fulfilled(value) { try { step(generator.next(value)); } catch (e) { reject(e); } }
        function rejected(value) { try { step(generator["throw"](value)); } catch (e) { reject(e); } }
        function step(result) { result.done ? resolve(result.value) : adopt(result.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, _arguments || [])).next());
    });
};
Object.defineProperty(exports, "__esModule", { value: true });
const mqtt = __importStar(require("mqtt"));
const fs = __importStar(require("fs"));
const client = mqtt.connect("mqtt://localhost:1883");
console.log('Connecting to MQTT broker...');
client.on("connect", () => {
    console.log('Connected to MQTT broker');
    client.subscribe("images", (err) => {
        if (!err) {
            console.log('Subscribed to topic "images"');
        }
        else {
            console.error('Error subscribing to topic "images":', err);
        }
    });
});
client.on('message', function (topic, message) {
    console.log('Message received on topic:', topic);
    ((innerTopic, innerMessage) => __awaiter(this, void 0, void 0, function* () {
        var fileName = '';
        console.log('Received message on topic:', innerTopic);
        for (var i = 0; i < 20; i++) {
            var nameByte = innerMessage[i];
            if (nameByte !== 0) {
                fileName += String.fromCharCode(innerMessage[i]);
            }
            else {
                break;
            }
        }
        var image = Buffer.from(innerMessage.subarray(20, innerMessage.length - 20));
        fs.writeFile(`receivedimages/${fileName}`, image, function (err) {
            if (err) {
                console.error('Error writing file:', err);
            }
            else {
                console.log('Image saved as image.jpg');
            }
        });
    }))(topic, message);
});
//# sourceMappingURL=app.js.map