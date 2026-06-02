#include <iostream>
#include <string>
#include <cstdlib>
#include <array>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cctype>
#include <unistd.h> 

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

void sanitize_all(std::string& s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) {
        return std::isspace(ch) || ch == '\n' || ch == '\r' || ch == '\t';
    }), s.end());
}

std::vector<std::string> split_keys(const std::string& str) {
    std::vector<std::string> keys;
    std::stringstream ss(str);
    std::string key;
    while (std::getline(ss, key, ',')) {
        sanitize_all(key);
        if (!key.empty()) {
            keys.push_back(key);
        }
    }
    return keys;
}

std::string execute_pipeline(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "Error opening pipe!";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string escape_json(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '\\': ss << "\\\\"; break;
            case '"':  ss << "\\\""; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:   ss << c; break;
        }
    }
    return ss.str();
}

void strip_markdown(std::string& str) {
    size_t pos;
    std::string btb = "`"; btb += "`"; btb += "`"; 
    std::string bbash = btb + "bash";
    std::string bjson = btb + "json";
    
    while ((pos = str.find(bbash)) != std::string::npos) str.erase(pos, 7);
    while ((pos = str.find(bjson)) != std::string::npos) str.erase(pos, 7);
    while ((pos = str.find(btb)) != std::string::npos) str.erase(pos, 3);
}

std::string read_file_content(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return "";
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::string parse_simple_json(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    size_t start = json.find("\"", pos);
    if (start == std::string::npos) return "";
    size_t end = json.find("\"", start + 1);
    if (end == std::string::npos) return "";
    return json.substr(start + 1, end - start - 1);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << RED << BOLD << "Error: " << RESET << "Missing prompt. Usage: ai_dev \"your prompt\"\n";
        return 1;
    }

    std::string home_dir = std::getenv("HOME");
    std::string config_path = home_dir + "/.ai_config.json";
    std::string raw_config = read_file_content(config_path);

    std::string api_key_str = "";
    char* api_key_env = std::getenv("GEMINI_API_KEY");
    if (api_key_env && !std::string(api_key_env).empty()) {
        api_key_str = std::string(api_key_env);
    } else if (!raw_config.empty()) {
        api_key_str = parse_simple_json(raw_config, "GEMINI_API_KEY");
    }

    if (api_key_str.empty()) {
        std::cerr << RED << BOLD << "[ERROR] " << RESET << "GEMINI_API_KEY missing from both Environment and ~/.ai_config.json!\n";
        return 1;
    }

    std::vector<std::string> api_keys = split_keys(api_key_str);
    if (api_keys.empty()) {
        std::cerr << RED << BOLD << "[ERROR] " << RESET << "No valid API keys found!\n";
        return 1;
    }

    std::string model_name = "";
    char* model_env = std::getenv("GEMINI_MODEL");
    if (model_env && !std::string(model_env).empty()) {
        model_name = std::string(model_env);
    } else if (!raw_config.empty()) {
        model_name = parse_simple_json(raw_config, "GEMINI_MODEL");
    }
    
    if (model_name.empty()) {
        model_name = "gemini-2.5-flash"; 
    }
    sanitize_all(model_name);

    std::string user_prompt = "";
    for (int i = 1; i < argc; ++i) {
        user_prompt += std::string(argv[i]) + " ";
    }

    std::string system_instruction = 
        "You are an expert autonomous Linux terminal agent loop. Your goal is to fulfill the user's request completely and efficiently.\n"
        "ENVIRONMENT NOTICE: The user interacts via Fish shell, but your scripts execute through a raw Bash subshell. Keep commands standard and universal.\n"
        "Instructions:\n"
        "1. You can run multiple exploration bash commands (e.g., ls, cat, grep, find) to inspect files automatically without user permission.\n"
        "2. Output ONLY the raw bash commands for the current step. Do NOT wrap them in markdown blocks.\n"
        "3. CRITICAL RULE FOR EDITS: Before you output any command that creates, writes, or edits files (like cat >, echo >, sed), you MUST start your response with a bash comment line in Bengali exactly matching: \"# আমি এই ফাইলগুলোকে একসাথে এডিট করবো: [file names]\" followed by the actual bash commands on the next lines.\n"
        "4. ONE-SHOT EDIT POLICY: You must combine ALL necessary file edits into a SINGLE, definitive edit response. Do NOT perform incremental or repetitive edits across multiple turns. Read and analyze all relevant files first during exploration, then make one comprehensive write/edit.\n"
        "5. Immediately after your edit action is successful, or if no changes are needed, output exactly \"DONE\" as your entire response.\n"
        "6. Do not loop or rewrite things you already fixed.\n";

    std::string project_files = execute_pipeline("ls -F");
    std::string contextual_prompt = system_instruction + "\nInitial Workspace Files:\n" + project_files + "\nUser Request: " + user_prompt + "\n\nExecution History:\n";

    int max_steps = 15; 
    int current_step = 1;
    size_t current_key_idx = 0;
    size_t consecutive_429_count = 0; 

    while (current_step <= max_steps) {
        std::string json_data = "{\"contents\": [{\"parts\": [{\"text\": \"" + escape_json(contextual_prompt) + "\"}]}]}";
        
        std::string json_file_path = home_dir + "/.gemini_payload.json";
        std::ofstream json_file(json_file_path);
        json_file << json_data;
        json_file.close();

        std::string response_file_path = home_dir + "/.gemini_response.json";
        std::string error_log_path = home_dir + "/.gemini_curl_err.log";
        
        std::string active_key = api_keys[current_key_idx];
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + model_name + ":generateContent?key=" + active_key;
        
        std::string curl_cmd = "curl -sS -L -X POST \"" + url + "\" -H \"Content-Type: application/json\" -d @" + json_file_path + " -o " + response_file_path + " 2>" + error_log_path;
        execute_pipeline(curl_cmd.c_str());
        std::remove(json_file_path.c_str()); 

        std::string jq_cmd = "jq -r '.candidates[0].content.parts[0].text' " + response_file_path + " 2>/dev/null";
        std::string generated_response = execute_pipeline(jq_cmd.c_str());

        if (generated_response.empty() || generated_response == "null\n") {
            std::string raw_api = read_file_content(response_file_path);
            
            if (raw_api.find("RESOURCE_EXHAUSTED") != std::string::npos || raw_api.find("\"code\": 429") != std::string::npos) {
                consecutive_429_count++;
                
                std::string masked = active_key.length() > 8 ? active_key.substr(0,4) + "..." + active_key.substr(active_key.length()-4) : "Key";
                std::cout << YELLOW << "\n[429 Quota Alert] Key [" << masked << "] exhausted or rate limited." << RESET << std::endl;
                
                current_key_idx = (current_key_idx + 1) % api_keys.size();
                std::string next_masked = api_keys[current_key_idx].length() > 8 ? api_keys[current_key_idx].substr(0,4) + "..." + api_keys[current_key_idx].substr(api_keys[current_key_idx].length()-4) : "Next Key";
                std::cout << CYAN << "[Key Rotation] Automatically shifting to key: [" << next_masked << "]" << RESET << std::endl;
                
                if (consecutive_429_count >= api_keys.size()) {
                    std::cout << YELLOW << "[All Keys Exhausted] Cycled through all keys. Sleeping 10s before retrying loop..." << RESET << std::endl;
                    sleep(10);
                    consecutive_429_count = 0; 
                }
                
                std::remove(response_file_path.c_str());
                std::remove(error_log_path.c_str());
                continue; 
            }
            
            std::cerr << RED << BOLD << "\n[API Error or Empty Response Detected]" << RESET << "\n";
            std::string curl_err = read_file_content(error_log_path);
            if (!curl_err.empty()) std::cerr << "Curl Network Error Log: " << RED << curl_err << RESET << "\n";
            return 1;
        }

        consecutive_429_count = 0; 
        std::remove(response_file_path.c_str());
        std::remove(error_log_path.c_str());

        strip_markdown(generated_response);
        while(!generated_response.empty() && (generated_response.back() == '\n' || generated_response.back() == ' ')) {
            generated_response.pop_back();
        }

        if (generated_response == "DONE") {
            std::cout << GREEN << BOLD << "\n[AI] Task completed successfully!\n" << RESET;
            break;
        }

        bool is_edit = (generated_response.find("আমি এই ফাইলগুলোকে একসাথে এডিট করবো") != std::string::npos);

        if (is_edit) {
            std::cout << CYAN << "\n[AI Automatic Edit Action]: " << GREEN << "Executing modifications natively..." << RESET << "\n";
        } else {
            std::cout << CYAN << "[Step " << current_step << " - Auto-Exploring] " << RESET << generated_response << "\n";
        }

        std::ofstream script(home_dir + "/.ai_tmp.sh");
        script << "#!/bin/bash\n" << generated_response;
        script.close();
        
        std::string cmd_output = execute_pipeline(("bash " + home_dir + "/.ai_tmp.sh 2>&1").c_str());
        contextual_prompt += "\nStep " + std::to_string(current_step) + ":\nCommand: " + generated_response + "\nTerminal Output:\n" + cmd_output + "\n";

        current_step++;
    }

    std::remove((home_dir + "/.ai_tmp.sh").c_str());
    return 0;
}