#!/bin/bash

REPO="git@github.com:some_user/some-ansible-repository.git"

# This SSH key should have read-only access to your repository
# Be careful about where to store this script or actually replace it
# with a better option.
cat >/root/.ssh/id_rsa <<EOF
INSERT PRIVATE KEY TO PULL CODE HERE
EOF

# Add GitHub to known_hosts
ssh-keyscan github.com >> /root/.ssh/known_hosts
chmod 0400 /root/.ssh/id_rsa /root/.ssh/known_hosts

# If you need to inject facts into Ansible for information about the environment
# such as the IPs of other VMs, etc, then create them via a template and inject them
# into S3, then pull them down for inclusion in your Ansible provision.yml file
# aws cli s3 cp s3://deployment_facts_bucket/deployment-facts.yaml /root/deployment-facts.yaml

# These are RHEL specific commands; you should change these to
# match your Linux flavour
yum update -y 
yum install git -y
pip install ansible
/usr/local/bin/ansible-pull provision.yml -C master -U $REPO -fi localhost, --full --purge
