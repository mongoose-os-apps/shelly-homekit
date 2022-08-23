/*
 * Copyright (c) Shelly-HomeKit Contributors
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package main

import (
	"bufio"
	"log"
	"os"
	"strings"
)

func parseLog(fname string, stockModelMap *StockModelMap) {
	file, err := os.Open(fname)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	numUnsupported := 0
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		parts := strings.Split(scanner.Text(), ` "`)
		mgosFWVersion := strings.Trim(parts[len(parts)-2], `"`)
		mgosDeviceID := strings.Trim(parts[len(parts)-1], `"`)
		hkModel, err := stockModelMap.GetHomeKitModel(mgosFWVersion, mgosDeviceID)
		if err != nil {
			log.Printf("%s", err)
			numUnsupported++
		}
		if false {
			log.Printf("line: %s|%s -> %s", mgosFWVersion, mgosDeviceID, hkModel)
		}
	}

	if err := scanner.Err(); err != nil {
		log.Fatal(err)
	}

	log.Printf("Unsupported   : %d", numUnsupported)
}
