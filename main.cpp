#include "Application.h"

#include <string>

int main() {
	/*std::cout << "Input a complex number: ";
	std::string cStr;
	std::getline(std::cin, cStr);
	unsigned r = std::stoi(cStr);
	unsigned i = std::stoi(cStr.substr(cStr.find('+') + 1));
	std::complex<double> c(r, i);*/

	Application app;
	return app.run();
}