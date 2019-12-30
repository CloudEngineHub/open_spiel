#!/bin/bash

# Copyright 2019 DeepMind Technologies Ltd. All rights reserved.
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

set -e
set -x

# Need this to be able to install TF >= 1.15 on Ubuntu 18.04.
sudo pip3 install --upgrade pip
sudo pip3 install --upgrade setuptools

virtualenv -p python ./venv
source ./venv/bin/activate

python --version

pip3 install --upgrade pip
pip3 install --upgrade setuptools
pip3 install -r requirements.txt -q

./open_spiel/scripts/build_and_run_tests.sh
deactivate
