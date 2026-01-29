import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as iot from 'aws-cdk-lib/aws-iot';
import { ThingWithCert } from 'cdk-iot-core-certificates-v3';
import { RestApi } from 'aws-cdk-lib/aws-apigateway';

export interface IoTDeviceStackProps extends cdk.StackProps {
    api: RestApi
}

export class IotDeviceStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: IoTDeviceStackProps) {
    super(scope, id, props);

    const thingName = "BootBootsThing";

    const { thingArn, certId, certPem, privKey } = new ThingWithCert(this, 'MyThing', {
      thingName: thingName,
      saveToParamStore: true,
      paramPrefix: 'BootsBoots',
    });

    const bbRole = new iam.Role(this, "BootBootsIamRole", {
      roleName: "BootBootsThingIamRole",
      description: "Used by BootBoots device to access Non-Iot AWS resoruces",
      assumedBy: new iam.ServicePrincipal("credentials.iot.amazonaws.com")
    });

    const bbPolicy = new iam.Policy(this, "BootBootsIamPolicy", {
      policyName: "BootBootsExtendedAccessPolicy",
      roles: [bbRole]
    });

    // Allow the IoT device to invoke any method on the BootBoots API
    bbPolicy.addStatements(new iam.PolicyStatement({
      actions: ["execute-api:Invoke"],
      effect: iam.Effect.ALLOW,
      resources: [props!.api.arnForExecuteApi('*', '/*', '*')]
    }));

    const bbRoleAlias = new iot.CfnRoleAlias(this, 'BootBootsRoleAlias', {
      roleAlias: "BootBootsRoleAlias",
      credentialDurationSeconds: 3600,
      roleArn: bbRole.roleArn
    });

    const bbIoTPolicy = new iot.CfnPolicy(this, 'IotAssumeRolePolicy', {
      policyName: 'AssumeRolePolicy',
      policyDocument: {
        Version: '2012-10-17',
        Statement: [
          {
            Effect: 'Allow',
            Action: 'iot:AssumeRoleWithCertificate',
            Resource: bbRoleAlias.attrRoleAliasArn,
          },
          {
            Effect: 'Allow',
            Action: 'iot:Connect',
            Resource: `arn:aws:iot:${this.region}:${this.account}:client/${thingName}`,
          },
          {
            Effect: 'Allow',
            Action: 'iot:Subscribe',
            Resource: `arn:aws:iot:${this.region}:${this.account}:topicfilter/catcam/*`,
          },
          {
            Effect: 'Allow',
            Action: ['iot:Publish', 'iot:Receive'],
            Resource: `arn:aws:iot:${this.region}:${this.account}:topic/catcam/*`,
          },
        ],
      },
    });

    // Construct certificate ARN from the certId returned by ThingWithCert
    const certArn = `arn:aws:iot:${this.region}:${this.account}:cert/${certId}`;

    // Attach the IoT policy to the certificate
    const bbPolicyPrincipalAttachment = new iot.CfnPolicyPrincipalAttachment(this, "BootBootsPolicyAttachment", {
      policyName: bbIoTPolicy.policyName!,
      principal: certArn
    });
    bbPolicyPrincipalAttachment.addDependency(bbIoTPolicy);

    // Note: ThingWithCert already attaches the certificate to the thing, so no ThingPrincipalAttachment needed

    new cdk.CfnOutput(this, 'ThingArn', {
      value: thingArn,
      description: 'The ARN of the IoT Thing'
    });

    new cdk.CfnOutput(this, 'CertificateArn', {
      value: certArn,
      description: 'The ARN of the IoT certificate'
    });

    new cdk.CfnOutput(this, 'CertificateId', {
      value: certId,
      description: 'The ID of the IoT certificate'
    });
  }
}