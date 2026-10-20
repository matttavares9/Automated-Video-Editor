# AWS Elastic Beanstalk Deployment

The deployed site is the Flask browser interface. It listens publicly on port
8000 and calls FastAPI, which runs privately with the C++ editor in the same
container. The MCP server is a local Codex adapter and is not deployed to AWS.

## Manual deployment

Before deploying, install the [Elastic Beanstalk CLI](https://docs.aws.amazon.com/elasticbeanstalk/latest/dg/eb-cli3-install.html),
configure AWS credentials, and select the `us-east-1` region. Set a billing
budget; the Elastic Beanstalk environment runs an EC2 instance and can incur
charges.

From the repository root, initialize the application and create the
development environment:

```bash
eb init ai-video-editor --platform docker --region us-east-1
eb create ai-video-editor-dev --single --instance_type t3.micro
eb open
```

Elastic Beanstalk builds the root `Dockerfile`. The `.ebextensions` health
check uses the app's `/health` endpoint. To check the deployed endpoint:

```bash
curl "$(eb status --verbose | awk '/CNAME/ {print $2}')/health"
```

For later updates, deploy from the repository root with:

```bash
eb deploy
```

## GitHub Actions deployment (CD)

The `deploy-aws.yml` workflow runs after CI succeeds for a push to `main`. It
packages that commit, deploys it to Elastic Beanstalk, and checks the deployed
version and health endpoint. It uses GitHub OIDC rather than stored AWS access
keys.

Configure a GitHub environment named `production` with:

- Secret `AWS_ROLE_ARN`: the ARN of an IAM role trusted by this repository's
  GitHub Actions identity and scoped to the required Elastic Beanstalk and S3
  deployment operations.
- Variable `EB_DEPLOY_BUCKET`: an S3 bucket in the deployment region.

Optional variables are `AWS_REGION` (default `us-east-1`),
`EB_APPLICATION_NAME` (default `ai-video-editor`), and
`EB_ENVIRONMENT_NAME` (default `ai-video-editor-dev`). A successful CI run is
required before the deployment workflow starts.

## Data durability

The demo uses SQLite and stores media under the container's `/data` directory.
These files are not durable if the instance is replaced. For persistent use,
configure PostgreSQL on Amazon RDS and store uploaded videos and exports in S3.
Keep credentials in AWS or GitHub secrets, never in the repository.
