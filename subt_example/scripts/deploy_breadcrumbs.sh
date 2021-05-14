#!/bin/bash

#
# Copyright (C) 2021 Open Robotics
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

# deploy_breadcrumbs.sh: A shell script to deploy multiple breadcrumbs.
#
# Deploy three breadcrumbs (1 per second).
# E.g.: ./deploy_breadcrumbs /X1/breadcrumb/deploy 3 1

set -e

# Usage function.
usage()
{
  echo "Usage: $0 <topic> <num_breadcrumbs> <elapsed>"
  echo "Arguments:"
  echo "  <topic>            Deploy topic (e.g.: /X1/breadcrumb/deploy)"
  echo "  <num_breadcrumbs>  Total number of breadcrumbs to deploy"
  echo "  <elapsed>          Number of seconds between deployments"
  exit 1
}

# Call usage() function if arguments not supplied.
[[ $# -ne 3 ]] && usage

TOPIC=$1
NUM_BREADCRUMBS=$2
DELAY=$3

for (( i=0; i<$NUM_BREADCRUMBS; i++ ))
do
  rostopic pub $TOPIC std_msgs/Empty "{}" --once
  echo "Deploying breadcrumb"
  sleep $DELAY
done
