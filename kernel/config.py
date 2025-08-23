#!/usr/bin/env python3

#
# Copyright (C) 2025 Germain Haugou
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
# Authors: Germain Haugou (germain.haugou@gmail.com)
#

from gvrun.target import BuildParameter

def declare(target, container):

    crt0 = BuildParameter(container, 'crt0', True, 'Add crt0').value

    if crt0:
        container.add_sources(['kernel/crt0.S'])

    container.add_sources([
        'kernel/init.c',
    ])
