// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

struct deepstream_elements;
class input_ingress_facade;

void setup_inference_performance_probes(input_ingress_facade &data, deepstream_elements &elements);
