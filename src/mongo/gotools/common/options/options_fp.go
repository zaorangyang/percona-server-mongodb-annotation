// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

// +build failpoints

package options

// EnableFailpoints does nothing if we've compiled with failpoints enabled
func EnableFailpoints(opts *ToolOptions) {
}
