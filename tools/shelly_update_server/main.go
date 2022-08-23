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
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"gopkg.in/yaml.v3"
)

var (
	flagStockModelMap   = flag.String("stock-model-map", "", "A file spcifying stock device id prefix -> HK model mapping; YAML or JSON")
	flagListenAddr      = flag.String("listen-addr", "", "Serve HTTP requests on this addr:port")
	flagRealIPHeader    = flag.String("real-ip-header", "", "Get remote client's address from this HTTP header")
	flagDestURLTemplate = flag.String("dest-url-template", "", "URL template to use for firmware redirects. %s is replaced with the model name.")
	flagLogRetention    = flag.Duration("log-retention", 5*time.Minute, "How long to retain device update logs")

	// Testing.
	flagParseLog = flag.String("parse-log", "", "Parse a log file instead of serving")
)

const (
	version = "1.0.0"
)

func main() {
	flag.Parse()

	log.Printf("%s version %s", filepath.Base(os.Args[0]), version)

	if *flagStockModelMap == "" {
		log.Fatal("Specify --stock-model-map to use")
	}

	log.Printf("Using stock model map from %s", *flagStockModelMap)
	data, err := ioutil.ReadFile(*flagStockModelMap)
	if err != nil {
		log.Fatal("Error reading --stock-model-map:", err)
	}
	var stockModelMap StockModelMap
	if err = yaml.Unmarshal(data, &stockModelMap); err != nil {
		log.Fatal("Invalid --stock-model-map file:", err)
	}

	if *flagParseLog != "" {
		parseLog(*flagParseLog, &stockModelMap)
		return
	}

	if *flagListenAddr == "" {
		log.Fatal("Specify --listen-addr to serve on")
	}
	if *flagDestURLTemplate == "" {
		log.Fatal("Specify --dest-url-template to use in redirects")
	} else {
		if !strings.Contains(*flagDestURLTemplate, "%s") {
			log.Fatal("--dest-url-template does not contain %s, this can't be right")
		}
		log.Printf("Will redirect to %s", *flagDestURLTemplate)
	}

	exitCleanup, exited := make(chan bool), make(chan bool)

	uh := newUpdateHandler(&stockModelMap, *flagRealIPHeader, *flagDestURLTemplate)
	server := &http.Server{
		Addr:    *flagListenAddr,
		Handler: uh,
	}
	go func() {
		log.Printf("Listening on %s", server.Addr)
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatal("ListenAndServe:", err)
		}
		exited <- true
	}()

	go func() {
		for {
			select {
			case <-time.Tick(*flagLogRetention / 2):
				uh.cleanupLogs(*flagLogRetention)
			case <-exitCleanup:
				exited <- true
				return
			}
		}
	}()

	sig := make(chan os.Signal)
	signal.Notify(sig, syscall.SIGINT)
	signal.Notify(sig, syscall.SIGTERM)
	select {
	case sig := <-sig:
		log.Printf(fmt.Sprintf("Got %s, stopping", sig))
		close(exitCleanup)
		ctx, cancel := context.WithTimeout(context.Background(), time.Duration(5*time.Second))
		defer cancel()
		server.Shutdown(ctx)
	}
	<-exited
	<-exited
	log.Printf("Exiting")
}
