#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <regex>
#include <cstdint>
#include <fstream>
#include <chrono>
#include <iomanip>

struct Scale {
    std::string name;
    std::vector<uint8_t> mask;
    
    Scale(const std::string& n, const std::vector<uint8_t>& m) : name(n), mask(m) {}
};

class Logger {
private:
    std::ofstream logFile;
    bool verbose;
    
public:
    Logger(bool v = true) : verbose(v) {
        // Create log file with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "scale_converter_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".log";
        
        logFile.open(ss.str());
        if (logFile.is_open()) {
            log("=== Scale Converter Log Started ===");
            log("Log file: " + ss.str());
        }
    }
    
    ~Logger() {
        if (logFile.is_open()) {
            log("=== Scale Converter Log Ended ===");
            logFile.close();
        }
    }
    
    void log(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 100000);
        
        std::string logMessage = "[" + timestamp + "] " + message;
        
        if (logFile.is_open()) {
            logFile << logMessage << std::endl;
            logFile.flush();
        }
        
        if (verbose) {
            std::cout << "LOG: " << message << std::endl;
        }
    }
    
    void error(const std::string& message) {
        log("ERROR: " + message);
        std::cerr << "ERROR: " << message << std::endl;
    }
    
    void warning(const std::string& message) {
        log("WARNING: " + message);
        std::cerr << "WARNING: " << message << std::endl;
    }
};

class ScaleConverter {
private:
    std::vector<Scale> inputScales;
    Logger* logger;
    
    // Convert mask from source EDO to target EDO using closest pitch matching
    std::vector<uint8_t> convertMask(const std::vector<uint8_t>& inputMask, int sourceEDO, int targetEDO) {
        std::vector<uint8_t> result(targetEDO, 0);
        
        // Find all active pitches in the input mask
        std::vector<int> activePitches;
        for (size_t i = 0; i < inputMask.size(); i++) {
            if (inputMask[i] == 1) {
                activePitches.push_back(i);
            }
        }
        
        // Map each active pitch to the closest pitch in target EDO
        for (int pitch : activePitches) {
            // Convert to fractional position (0.0 to 1.0) based on source EDO
            double fractionalPos = (double)pitch / (double)sourceEDO;
            
            // Find closest position in target EDO
            int closestPos = (int)std::round(fractionalPos * targetEDO) % targetEDO;
            
            result[closestPos] = 1;
        }
        
        return result;
    }
    
    // Parse a single scale definition line
    Scale parseScaleLine(const std::string& line) {
        // Remove leading/trailing whitespace and carriage returns
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r") + 1);
        
        if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '*') {
            return Scale("", {}); // Skip comments and empty lines
        }
        
        // Find the opening brace for the mask (after the comma)
        size_t commaPos = trimmed.find(',');
        if (commaPos == std::string::npos) {
            return Scale("", {});
        }
        
        // Extract name (everything before the comma)
        std::string name = trimmed.substr(0, commaPos);
        
        // Remove trailing whitespace from name
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        
        // Remove quotes from name if present
        if (name.length() >= 2 && name.front() == '"' && name.back() == '"') {
            name = name.substr(1, name.length() - 2);
        }
        
        // Remove any remaining braces or quotes
        while (!name.empty() && (name.front() == '{' || name.front() == '"')) {
            name = name.substr(1);
        }
        while (!name.empty() && (name.back() == '"')) {
            name.pop_back();
        }
        
        
        // Find the mask array (between the braces after comma)
        size_t maskStart = trimmed.find('{', commaPos);
        size_t maskEnd = trimmed.find_last_of('}');
        if (maskStart == std::string::npos || maskEnd == std::string::npos || maskStart >= maskEnd) {
            return Scale("", {});
        }
        
        std::string maskStr = trimmed.substr(maskStart + 1, maskEnd - maskStart - 1);
        
        // Parse the mask array
        std::vector<uint8_t> mask;
        std::istringstream iss(maskStr);
        std::string token;
        
        while (std::getline(iss, token, ',')) {
            // Remove whitespace from token
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            
            if (!token.empty()) {
                try {
                    int value = std::stoi(token);
                    mask.push_back(value ? 1 : 0);
                } catch (const std::exception&) {
                    // Skip invalid tokens
                }
            }
        }
        
        return Scale(name, mask);
    }
    
public:
    ScaleConverter(Logger* l = nullptr) : logger(l) {}
    
    // Detect the EDO from input scales (assumes all scales have the same EDO)
    int detectInputEDO() const {
        if (inputScales.empty()) {
            return 12; // Default to 12-EDO
        }
        
        // Use the first scale's mask size as the EDO
        return inputScales[0].mask.size();
    }
    
    void loadScales(const std::string& input) {
        if (logger) logger->log("Loading scales from input");
        inputScales.clear();
        std::istringstream iss(input);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (logger) logger->log("Processing line: " + line);
            Scale scale = parseScaleLine(line);
            if (!scale.name.empty() && !scale.mask.empty()) {
                inputScales.push_back(scale);
                if (logger) logger->log("Added scale: " + scale.name + " (mask size: " + std::to_string(scale.mask.size()) + ")");
            } else {
                if (logger) logger->warning("Skipped invalid scale definition");
            }
        }
        if (logger) logger->log("Loaded " + std::to_string(inputScales.size()) + " scales total");
    }
    
    std::string generateForEDO(int targetEDO) {
        int sourceEDO = detectInputEDO();
        if (logger) logger->log("Converting from " + std::to_string(sourceEDO) + "-EDO to " + std::to_string(targetEDO) + "-EDO");
        std::ostringstream oss;
        
        for (size_t j = 0; j < inputScales.size(); j++) {
            const auto& scale = inputScales[j];
            if (logger) logger->log("Converting scale " + std::to_string(j+1) + "/" + std::to_string(inputScales.size()) + ": " + scale.name);
            
            std::vector<uint8_t> convertedMask = convertMask(scale.mask, sourceEDO, targetEDO);
            
            // Format as C++ array initializer
            oss << "{\"" << scale.name << "\", {";
            for (size_t i = 0; i < convertedMask.size(); i++) {
                oss << (int)convertedMask[i];
                if (i < convertedMask.size() - 1) {
                    oss << ",";
                }
            }
            oss << "}}";
            if (j < inputScales.size() - 1) {
                oss << ",";
            }
            oss << "\n";
        }
        
        return oss.str();
    }
    
    bool writeToFile(const std::string& content, const std::string& filename) {
        if (logger) logger->log("Writing output to file: " + filename);
        std::ofstream file(filename);
        if (!file.is_open()) {
            if (logger) logger->error("Failed to open output file: " + filename);
            return false;
        }
        
        file << content;
        file.close();
        
        if (logger) logger->log("Successfully wrote " + std::to_string(content.length()) + " characters to " + filename);
        return true;
    }
    
    std::string generateForEDORange(int startEDO, int endEDO) {
        int sourceEDO = detectInputEDO();
        if (logger) logger->log("Converting from " + std::to_string(sourceEDO) + "-EDO to range " + std::to_string(startEDO) + "-" + std::to_string(endEDO) + "-EDO");
        std::ostringstream oss;
        
        for (int targetEDO = startEDO; targetEDO <= endEDO; targetEDO++) {
            oss << "// " << targetEDO << "-EDO scales\n";
            oss << "const int NUM_SCALES_" << targetEDO << "EDO = " << inputScales.size() << ";\n";
            oss << "static const Scale SCALES_" << targetEDO << "EDO[] = {\n";
            
            for (const auto& scale : inputScales) {
                std::vector<uint8_t> convertedMask = convertMask(scale.mask, sourceEDO, targetEDO);
                
                // Format as C++ array initializer
                oss << "    {\"" << scale.name << "\", {";
                for (size_t i = 0; i < convertedMask.size(); i++) {
                    oss << (int)convertedMask[i];
                    if (i < convertedMask.size() - 1) {
                        oss << ",";
                    }
                }
                oss << "}}";
                if (&scale != &inputScales.back()) {
                    oss << ",";
                }
                oss << "\n";
            }
            
            oss << "};\n\n";
        }
        
        return oss.str();
    }
    
    void printInputScales() {
        int detectedEDO = detectInputEDO();
        std::cout << "Loaded " << inputScales.size() << " scales from " << detectedEDO << "-EDO:\n";
        for (const auto& scale : inputScales) {
            std::cout << "- " << scale.name << " (mask size: " << scale.mask.size() << ")\n";
        }
    }
    
    size_t getInputScaleCount() const {
        return inputScales.size();
    }
};

int main(int argc, char* argv[]) {
    std::cout << "=== 12-EDO to N-EDO Scale Converter ===\n\n";
    
    // Create logger
    Logger logger(true);
    logger.log("Program started");
    
    ScaleConverter converter(&logger);
    
    std::string input;
    
    // Check if input file is provided as command line argument
    if (argc > 1) {
        std::ifstream file(argv[1]);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                input += line + "\n";
            }
            file.close();
            std::cout << "Loaded scales from file: " << argv[1] << "\n\n";
        } else {
            std::cout << "Error: Could not open file " << argv[1] << "\n";
            return 1;
        }
    } else {
        // Show startup menu
        std::cout << "Choose input method:\n";
        std::cout << "1. Use input.txt file\n";
        std::cout << "2. Enter scales manually\n";
        std::cout << "Enter choice (1-2): ";
        
        int inputChoice;
        std::cin >> inputChoice;
        std::cin.ignore(); // Clear newline
        
        logger.log("User selected input method: " + std::to_string(inputChoice));
        
        if (inputChoice == 1) {
            // Try to load from input.txt
            std::ifstream file("input.txt");
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    input += line + "\n";
                }
                file.close();
                std::cout << "Loaded scales from input.txt\n\n";
                logger.log("Loaded scales from input.txt");
            } else {
                std::cout << "Error: Could not open input.txt file.\n";
                std::cout << "Make sure input.txt exists in the current directory.\n";
                logger.error("Failed to open input.txt file");
                return 1;
            }
        } else if (inputChoice == 2) {
            // Manual entry (original behavior)
            std::cout << "\nPaste your 12-EDO scale definitions (one per line, ending with empty line):\n";
            std::cout << "Format: {\"Scale Name\", {1,0,1,0,1,1,0,1,0,1,0,1}}}\n\n";
            
            std::string line;
            
            // Read input until empty line
            while (std::getline(std::cin, line)) {
                if (line.empty()) {
                    break;
                }
                input += line + "\n";
            }
            logger.log("User entered scales manually");
        } else {
            std::cout << "Invalid choice. Please run the program again.\n";
            logger.error("Invalid input method choice: " + std::to_string(inputChoice));
            return 1;
        }
    }
    
    if (input.empty()) {
        std::cout << "No input provided. Exiting.\n";
        return 1;
    }
    
    converter.loadScales(input);
    converter.printInputScales();
    
    std::cout << "\nChoose conversion mode:\n";
    std::cout << "1. Single EDO value\n";
    std::cout << "2. Range of EDO values (e.g., 13-120)\n";
    std::cout << "3. Multiple individual EDO values (comma-separated)\n";
    
    int choice;
    std::cout << "Enter choice (1-3): ";
    std::cin >> choice;
    logger.log("Read choice: " + std::to_string(choice));
    std::cin.ignore(); // Clear the newline from cin
    
    std::string result;
    std::string outputFilename;
    
    try {
        switch (choice) {
            case 1: {
                int edo;
                std::cout << "Enter target EDO: ";
                if (!(std::cin >> edo)) {
                    logger.error("Failed to read EDO input - stream error");
                    std::cout << "Invalid input. Please enter a number.\n";
                    return 1;
                }
                logger.log("Read EDO input: " + std::to_string(edo));
                
                if (edo < 1 || edo > 120) {
                    logger.error("Invalid EDO: " + std::to_string(edo) + ". Must be between 1 and 120.");
                    std::cout << "Invalid EDO. Must be between 1 and 120.\n";
                    return 1;
                }
                
                logger.log("Converting to single EDO: " + std::to_string(edo));
                result = converter.generateForEDO(edo);
                std::cout << "\n=== Generated " << edo << "-EDO scales ===\n";
                break;
            }
        
            case 2: {
                int startEDO, endEDO;
                std::cout << "Enter start EDO: ";
                std::cin >> startEDO;
                std::cout << "Enter end EDO: ";
                std::cin >> endEDO;
                
                if (startEDO < 1 || endEDO > 120 || startEDO > endEDO) {
                    logger.error("Invalid range: " + std::to_string(startEDO) + "-" + std::to_string(endEDO));
                    std::cout << "Invalid range. EDOs must be between 1 and 120, start <= end.\n";
                    return 1;
                }
                
                logger.log("Converting to EDO range: " + std::to_string(startEDO) + "-" + std::to_string(endEDO));
                result = converter.generateForEDORange(startEDO, endEDO);
                std::cout << "\n=== Generated scales for EDOs " << startEDO << "-" << endEDO << " ===\n";
                break;
            }
        
            case 3: {
                std::string edosStr;
                std::cout << "Enter EDO values (comma-separated, e.g., 13,17,19,22): ";
                std::cin.ignore(); // Clear newline
                std::getline(std::cin, edosStr);
                
                logger.log("Processing multiple EDOs: " + edosStr);
                
                std::vector<int> edos;
                std::istringstream iss(edosStr);
                std::string token;
                
                while (std::getline(iss, token, ',')) {
                    try {
                        int edo = std::stoi(token);
                        if (edo >= 1 && edo <= 120) {
                            edos.push_back(edo);
                            logger.log("Added EDO: " + std::to_string(edo));
                        } else {
                            logger.warning("Skipped invalid EDO: " + std::to_string(edo));
                        }
                    } catch (const std::exception& e) {
                        logger.warning("Failed to parse EDO token: " + token + " - " + e.what());
                    }
                }
                
                if (edos.empty()) {
                    logger.error("No valid EDO values provided");
                    std::cout << "No valid EDO values provided.\n";
                    return 1;
                }
                
                std::ostringstream oss;
                for (int edo : edos) {
                    logger.log("Generating scales for EDO: " + std::to_string(edo));
                    oss << "// " << edo << "-EDO scales\n";
                    oss << "const int NUM_SCALES_" << edo << "EDO = " << converter.getInputScaleCount() << ";\n";
                    oss << "static const Scale SCALES_" << edo << "EDO[] = {\n";
                    oss << converter.generateForEDO(edo);
                    oss << "};\n\n";
                }
                
                result = oss.str();
                std::cout << "\n=== Generated scales for specified EDOs ===\n";
                break;
            }
            
            default:
                logger.error("Invalid choice: " + std::to_string(choice));
                std::cout << "Invalid choice.\n";
                return 1;
        }
        
        // Ask for output filename after processing
        std::cout << "\nEnter output filename (or press Enter for 'output.txt'): ";
        std::cin.ignore(); // Clear any remaining newline
        std::getline(std::cin, outputFilename);
        if (outputFilename.empty()) {
            outputFilename = "output.txt";
        }
        logger.log("Output filename: " + outputFilename);
        
        // Write result to file
        logger.log("Writing result to file: " + outputFilename);
        if (converter.writeToFile(result, outputFilename)) {
            std::cout << "\n=== Output written to " << outputFilename << " ===\n";
            std::cout << "Result preview (first 500 characters):\n";
            std::cout << result.substr(0, 500);
            if (result.length() > 500) {
                std::cout << "\n... (truncated, see file for full output)";
            }
            std::cout << "\n";
        } else {
            std::cout << "\n=== Failed to write to file, displaying result ===\n";
            std::cout << result << "\n";
        }
        
        std::cout << "=== End of generated scales ===\n";
        logger.log("Program completed successfully");
        
    } catch (const std::exception& e) {
        logger.error("Exception caught: " + std::string(e.what()));
        std::cout << "\nERROR: " << e.what() << "\n";
        std::cout << "Check the log file for more details.\n";
        return 1;
    }
    
    return 0;
}
