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
	"fmt"
	"log"
	"net/http"
	"strings"
	"sync"
	"time"
)

const (
	fwVersionHeader  = "X-MGOS-FW-Version"
	deviceIDHeader   = "X-MGOS-Device-ID"
	unsupportedModel = "x"
)

type StockModelMap struct {
	Models map[string]string `yaml:"models"`
}

// GetHomeKitModel returns the HK model based on X-MGOS-FW-Version and X-MGOS-Device-ID headers
// sent by the stock device when fetching an update.
func (smm *StockModelMap) GetHomeKitModel(mgosFWVersion, mgosDeviceID string) (string, error) {
	if mgosFWVersion == "-" {
		return "", fmt.Errorf("device did not send model information")
	}
	parts := strings.Split(mgosDeviceID, " ")
	if len(parts) != 2 {
		return "", fmt.Errorf("unsupported device ID %q", mgosDeviceID)
	}
	devID := parts[0]
	devModel := strings.Split(devID, "-")[0]
	hkModel := smm.Models[devModel]
	if hkModel == unsupportedModel {
		return "", fmt.Errorf("device model %q is not yet supported", devModel)
	}
	if hkModel == "" {
		log.Printf("unknown model: %s %s", mgosFWVersion, mgosDeviceID)
		return "", fmt.Errorf("unknown device model %q", devModel)
	}
	return hkModel, nil
}

type UpdateHandler struct {
	realIPHeader    string
	destURLTemplate string
	stockModelMap   *StockModelMap

	logs     map[string][]logEntry
	logsLock sync.Mutex
}

type logEntry struct {
	ts  time.Time
	msg string
}

func (le logEntry) String() string {
	return fmt.Sprintf("%s | %s", le.ts.Format(time.Stamp), le.msg)
}

func (uh *UpdateHandler) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	defer req.Body.Close()
	remoteAddr := req.RemoteAddr
	if rip := req.Header.Get(uh.realIPHeader); rip != "" {
		remoteAddr = rip
	} else {
		remoteAddr = strings.Split(remoteAddr, ":")[0] // Strip the port
	}
	switch req.URL.Path {
	case "/update":
		fwVersion, deviceID := req.Header.Get(fwVersionHeader), req.Header.Get(deviceIDHeader)
		if fwVersion != "" && deviceID != "" {
			uh.serveFirmware(remoteAddr, fwVersion, deviceID, w)
			return
		}
		if ua := req.Header.Get("User-Agent"); ua != "" {
			// Request from a browser, be nice.
			w.Header().Set("Content-Type", `text/html; charset="UTF-8"`)
			uh.addLogEntry(remoteAddr, fmt.Sprintf("index request from %q", ua))
			fmt.Fprintf(w, "Please follow instructions <a href='https://github.com/mongoose-os-apps/shelly-homekit/'>here</a>.\n")
		} else {
			http.Error(w, "Not Found", http.StatusNotFound)
		}
	case "/log":
		uh.serveLog(remoteAddr, w)
	default:
		uh.addLogEntry(remoteAddr, fmt.Sprintf("unknown request %q", req.URL.Path))
		http.Error(w, "Not Found", http.StatusNotFound)
	}
}

func (uh *UpdateHandler) addLogEntry(remoteAddr string, msg string) {
	log.Printf("%s: %s", remoteAddr, msg)
	uh.logsLock.Lock()
	uh.logs[remoteAddr] = append(uh.logs[remoteAddr], logEntry{ts: time.Now(), msg: msg})
	uh.logsLock.Unlock()
}

func (uh *UpdateHandler) serveFirmware(remoteAddr, fwVersion, deviceID string, w http.ResponseWriter) {
	hkModel, err := uh.stockModelMap.GetHomeKitModel(fwVersion, deviceID)
	if err != nil {
		uh.addLogEntry(remoteAddr, fmt.Sprintf("firmware request: %q %q -> %s", fwVersion, deviceID, err))
		http.Error(w, "Not Found", http.StatusNotFound)
		return
	}
	url := fmt.Sprintf(uh.destURLTemplate, hkModel)
	uh.addLogEntry(remoteAddr, fmt.Sprintf("firmware request: %q %q -> %s", fwVersion, deviceID, url))
	w.Header().Set("Location", url)
	http.Error(w, fmt.Sprintf("You are being taken to %s", url), http.StatusFound)
}

func (uh *UpdateHandler) serveLog(remoteAddr string, w http.ResponseWriter) {
	uh.addLogEntry(remoteAddr, fmt.Sprintf("log request"))
	w.Header().Set("Content-Type", `text/html; charset="UTF-8"`)
	fmt.Fprintf(w, "<h1>History for %s</h1>\n", remoteAddr)
	uh.logsLock.Lock()
	if entries, ok := uh.logs[remoteAddr]; ok {
		fmt.Fprintf(w, "<ul>\n")
		for _, le := range entries {
			fmt.Fprintf(w, "  <li><pre style='margin: 0'>%s</pre></li>\n", le)
		}
		fmt.Fprintf(w, "</ul>\n")
	}
	uh.logsLock.Unlock()
	fmt.Fprintf(w, "<hr>\n")
	fmt.Fprintf(w, "%s\n", time.Now().Format(time.Stamp))
}

func (uh *UpdateHandler) cleanupLogs(maxAge time.Duration) {
	uh.logsLock.Lock()
	defer uh.logsLock.Unlock()
	threshold := time.Now().Add(-maxAge)
	for remoteAddr, he := range uh.logs {
		cutoff := -1
		for i, le := range he {
			if threshold.Before(le.ts) {
				break
			}
			cutoff = i
		}
		if cutoff >= 0 {
			if cutoff < len(he)-1 {
				uh.logs[remoteAddr] = he[cutoff+1:]
			} else {
				delete(uh.logs, remoteAddr)
			}
		}
	}
}

func newUpdateHandler(stockModelMap *StockModelMap, realIPHeader, destURLTemplate string) *UpdateHandler {
	return &UpdateHandler{
		realIPHeader:    realIPHeader,
		destURLTemplate: destURLTemplate,
		stockModelMap:   stockModelMap,
		logs:            make(map[string][]logEntry),
	}
}
