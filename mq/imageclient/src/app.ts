import * as mqtt from 'mqtt';
import * as fs from 'fs';


const client = mqtt.connect("mqtt://localhost:1883");
console.log('Connecting to MQTT broker...');
client.on("connect", () => {
    console.log('Connected to MQTT broker');
    client.subscribe("images", (err) => {
        if (!err) {
            console.log('Subscribed to topic "images"');
        } else {
            console.error('Error subscribing to topic "images":', err);
        }
    });
});

client.on('message', function (topic, message) {
    (async (innerTopic, innerMessage) => {

        try {
            var fileName: String = '';
            console.log(`Image redeived on topic: ${innerTopic}`);
            for (var i: number = 0; i < 20; i++) {
                var nameByte: number = innerMessage[i];
                if (nameByte !== 0) {
                    fileName += String.fromCharCode(innerMessage[i]);
                } else {
                    break
                }
            }

            var image = Buffer.from(innerMessage.subarray(20, innerMessage.length - 20));

            fs.writeFileSync(`receivedimages/${fileName}`, image);
            console.log(`Image saved as ${fileName} at ${new Date().toLocaleDateString()} ${new Date().toLocaleTimeString()}`);
        } catch (error) {
            console.error('Error processing message:', error);
        }

    })(topic, message);
});