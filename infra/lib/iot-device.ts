import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as iot from 'aws-cdk-lib/aws-iot';
import { ThingWithCert } from 'cdk-iot-core-certificates-v3';

export class IotDeviceStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    const thingName = "BootBootsThing";

    const { thingArn, certId, certPem, privKey } = new ThingWithCert(this, 'MyThing', {
      thingName: thingName,
      saveToParamStore: true,
      paramPrefix: 'BootsBoots',
    });

    const bbRole = new iam.Role(this, "BootBootsIamRole", {
      description: "Used by BootBoots device to access Non-Iot AWS resoruces",
      assumedBy: new iam.ServicePrincipal("iot.amazonaws.com")
    });

    const bbPolicy = new iam.Policy(this, "BootBootsIamPolicy", {
      policyName: "BootBootsExtendedAccessPolicy",
      roles: [bbRole]
    });

    bbPolicy.addStatements(new iam.PolicyStatement({
      actions: ["ec2:DescribeAccountAttributes"],
      effect: iam.Effect.ALLOW,
      resources: ["*"]
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
        Statement: [{
          Effect: 'Allow',
          Action: 'iot:AssumeRoleWithCertificate',
          Resource: bbRoleAlias.attrRoleAliasArn,
        }],
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