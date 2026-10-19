# AWS Elastic Beanstalk deployment

This repository deploys its Dockerized **Flask browser interface** on **AWS
Elastic Beanstalk**. The Flask process is public on port 8000 and calls a
FastAPI/C++ editing service bound privately inside the same container on port
8001. Docker Compose is for local development; for production, deploy the API
image to ECS Fargate and keep Django/Flask as separate services only when
those interfaces need to be public.

The Codex MCP adapter is intentionally local-only. It is neither started nor
included in the Elastic Beanstalk image.

## Before deploying

1. Create an AWS account and set a billing budget/alarm. Free Tier benefits or
   promotional credits are account-specific and time-limited; a running EC2
   instance can incur charges after those benefits are exhausted.
2. Install the EB CLI: `pip install awsebcli`, or create an ECS cluster/service
   if using the GitHub Actions deployment workflow.
3. Authenticate locally with `aws configure` using an IAM identity that can
   administer Elastic Beanstalk, EC2, CloudFormation, and S3 for this project.

## First deployment

From the repository root:

```bash
eb init fastapi-blur-trimmer --platform docker --region us-east-1
eb create fastapi-blur-trimmer-dev --single --instance_type t3.micro
eb setenv DATA_DIR=/data VIDEO_ENGINE_BIN=/app/build/video_engine
eb open
```

Elastic Beanstalk detects the root `Dockerfile`; `.ebextensions/01-healthcheck.config`
uses Flask's `/health` endpoint for environment health. Open the environment
URL to reach the browser editor, then confirm it responds at:

The root `docker-compose.yml` is deliberately excluded by `.ebignore`: it is
only the local development stack and must not override the production
Dockerfile on Elastic Beanstalk.

```bash
curl "$(eb status --verbose | grep CNAME | awk '{print $2}')/health"
```

For subsequent deployments:

```bash
eb deploy
```

## Data and production hardening

The first deployment intentionally uses SQLite in the container. It is suitable
for a demo but its database and uploaded videos are **not durable** if the
instance is replaced or the environment is recreated. Before accepting real
customer uploads, make these two changes:

1. Create Amazon RDS PostgreSQL and set `DATABASE_URL` to its private
   connection URL (the API automatically accepts PostgreSQL URLs).
2. Store originals and exports in S3 instead of the container `/data`
   directory, then give the EC2 instance role least-privilege access to that
   bucket.

Use a separate staging environment, restrict inbound access as appropriate,
and keep API keys in Elastic Beanstalk environment properties or AWS Secrets
Manager—never in this repository.

## GitHub Actions deployment to Elastic Beanstalk

The `.github/workflows/deploy-aws.yml` workflow runs after CI succeeds for a
push to `main`. It packages the tested commit, deploys an Elastic Beanstalk
application version, and checks the resulting version and Flask health endpoint.
It uses GitHub OIDC instead of long-lived access keys. Create a narrowly scoped
IAM role trusted by the repository's `production` GitHub environment, with
permission to upload to a deployment bucket and create, update, and describe
versions of this Elastic Beanstalk application. Configure these values in the
GitHub `production` environment:

- secret `AWS_ROLE_ARN`
- variable `EB_DEPLOY_BUCKET` (an S3 bucket in the same AWS region)

The optional variables `AWS_REGION`, `EB_APPLICATION_NAME`, and
`EB_ENVIRONMENT_NAME` default to `us-east-1`, `ai-video-editor`, and
`ai-video-editor-dev`, respectively.

Before enabling the workflow, commit the current browser deployment changes,
including `start-web.sh` and the Dockerfile. The workflow refuses to deploy a
commit missing that startup configuration. A successful CI run and deployment
must be verified in GitHub Actions; committing this file alone does not activate
the required AWS permissions or prove that CD works.
