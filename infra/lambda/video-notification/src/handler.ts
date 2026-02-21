import { S3Event, Context } from 'aws-lambda';
import { Logger } from '@aws-lambda-powertools/logger';
import { S3Client, GetObjectCommand } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';
import { SESClient, SendEmailCommand } from '@aws-sdk/client-ses';
import { SNSClient, PublishCommand } from '@aws-sdk/client-sns';

const logger = new Logger({ serviceName: 'video-notification' });
const s3Client = new S3Client({ region: process.env.AWS_REGION });
const sesClient = new SESClient({ region: process.env.AWS_REGION });
const snsClient = new SNSClient({ region: process.env.AWS_REGION });

const SIGNED_URL_EXPIRY_SECONDS = 30 * 24 * 60 * 60; // 30 days

export async function handler(event: S3Event, _context: Context): Promise<void> {
    logger.info('Video notification Lambda invoked', {
        records: event.Records.length
    });

    for (const record of event.Records) {
        const bucket = record.s3.bucket.name;
        const key = decodeURIComponent(record.s3.object.key.replace(/\+/g, ' '));
        const size = record.s3.object.size;
        const eventTime = record.eventTime;

        logger.info('Processing S3 event', { bucket, key, size, eventTime });

        try {
            // Generate pre-signed URL valid for 30 days
            const command = new GetObjectCommand({
                Bucket: bucket,
                Key: key
            });

            const signedUrl = await getSignedUrl(s3Client, command, {
                expiresIn: SIGNED_URL_EXPIRY_SECONDS
            });

            logger.info('Generated pre-signed URL', {
                key,
                expiresInDays: 30
            });

            // Extract filename from key (catcam-videos/filename.avi -> filename.avi)
            const filename = key.split('/').pop() || key;

            // Format file size
            const fileSizeKB = Math.round(size / 1024);
            const fileSizeMB = (size / (1024 * 1024)).toFixed(2);
            const sizeDisplay = size > 1024 * 1024 ? `${fileSizeMB} MB` : `${fileSizeKB} KB`;

            // Format timestamp
            const uploadTime = new Date(eventTime).toLocaleString('en-GB', {
                timeZone: 'Europe/London',
                dateStyle: 'full',
                timeStyle: 'long'
            });

            // Send email via SES
            const recipientEmail = process.env.NOTIFICATION_EMAIL;
            const senderEmail = process.env.SENDER_EMAIL;

            if (!recipientEmail || !senderEmail) {
                throw new Error('NOTIFICATION_EMAIL or SENDER_EMAIL environment variable not set');
            }

            const emailSubject = `BootBoots Alert: Boots Detected! Video Captured`;

            const emailBody = `
BootBoots Cat Deterrent System - Video Alert

A video has been captured by the BootBoots system after detecting Boots!

Video Details:
- Filename: ${filename}
- Size: ${sizeDisplay}
- Captured: ${uploadTime}

Watch the video (link valid for 30 days):
${signedUrl}

---
This is an automated message from the BootBoots Cat Deterrent System.
            `.trim();

            const htmlBody = `
<!DOCTYPE html>
<html>
<head>
    <style>
        body { font-family: Arial, sans-serif; line-height: 1.6; color: #333; }
        .container { max-width: 600px; margin: 0 auto; padding: 20px; }
        .header { background: #e74c3c; color: white; padding: 20px; text-align: center; border-radius: 8px 8px 0 0; }
        .content { background: #f9f9f9; padding: 20px; border: 1px solid #ddd; }
        .details { background: white; padding: 15px; border-radius: 4px; margin: 15px 0; }
        .details dt { font-weight: bold; color: #555; }
        .details dd { margin: 0 0 10px 0; }
        .button { display: inline-block; background: #3498db; color: white; padding: 12px 24px; text-decoration: none; border-radius: 4px; margin: 15px 0; }
        .button:hover { background: #2980b9; }
        .footer { text-align: center; color: #888; font-size: 12px; margin-top: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🐱 Boots Detected!</h1>
            <p>Video captured by BootBoots</p>
        </div>
        <div class="content">
            <p>The BootBoots Cat Deterrent System has detected Boots and captured a video of the event.</p>

            <div class="details">
                <dl>
                    <dt>Filename</dt>
                    <dd>${filename}</dd>
                    <dt>Size</dt>
                    <dd>${sizeDisplay}</dd>
                    <dt>Captured</dt>
                    <dd>${uploadTime}</dd>
                </dl>
            </div>

            <p style="text-align: center;">
                <a href="${signedUrl}" class="button">Watch Video</a>
            </p>

            <p style="font-size: 12px; color: #666;">
                This link will expire in 30 days. Right-click and "Save link as..." to download.
            </p>
        </div>
        <div class="footer">
            <p>Automated message from BootBoots Cat Deterrent System</p>
        </div>
    </div>
</body>
</html>
            `.trim();

            const sendEmailCommand = new SendEmailCommand({
                Source: senderEmail,
                Destination: {
                    ToAddresses: [recipientEmail]
                },
                Message: {
                    Subject: {
                        Data: emailSubject,
                        Charset: 'UTF-8'
                    },
                    Body: {
                        Text: {
                            Data: emailBody,
                            Charset: 'UTF-8'
                        },
                        Html: {
                            Data: htmlBody,
                            Charset: 'UTF-8'
                        }
                    }
                }
            });

            await sesClient.send(sendEmailCommand);

            logger.info('Email notification sent successfully', {
                recipient: recipientEmail,
                filename,
                size: sizeDisplay
            });

            // Send SMS via SNS direct SMS
            const notificationPhone = process.env.NOTIFICATION_PHONE;

            if (notificationPhone) {
                const smsBody = `Boots detected! Video (${sizeDisplay}, ${uploadTime}): ${signedUrl}`;

                await snsClient.send(new PublishCommand({
                    PhoneNumber: notificationPhone,
                    Message: smsBody,
                    MessageAttributes: {
                        'AWS.SNS.SMS.SMSType': {
                            DataType: 'String',
                            StringValue: 'Transactional',
                        },
                    },
                }));
                logger.info('SMS notification sent successfully', { phone: notificationPhone });
            } else {
                logger.warn('Skipping SMS: NOTIFICATION_PHONE not set');
            }

        } catch (error: any) {
            logger.error('Failed to process video notification', {
                error: error.message,
                stack: error.stack,
                bucket,
                key
            });
            throw error;
        }
    }
}
