#pragma once
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <complex>
#include <iostream>
#include <thread>
#include <vector>

class Application {
private:
	unsigned width = 1400;
	unsigned height = 1400;

	unsigned maxIterations = 100;

	unsigned maxThreads = std::thread::hardware_concurrency();

	sf::RenderWindow window;

	std::complex<double> translation;

	double Zoom;

	struct color {
		float R, G, B;

		color operator*(float c) {
			return { std::min(c * R, 1.f), std::min(c * G, 1.f) , std::min(c * B, 1.f) };
		}

		color operator+(color snd) {
			return { std::min(R + snd.R, 1.f), std::min(G + snd.G, 1.f), std::min(B + snd.B, 1.f) };
		}
	};

	struct pixel {
		unsigned char R, G, B, A;

		pixel() = default;
		pixel(color c) : R(255 * c.R), G(255 * c.G), B(255 * c.B), A(255) {}
	};

	std::vector<pixel> pixels;

	sf::Texture texture;
	sf::Sprite sprite;
	
	bool leftPressed;
	sf::Vector2i lastMousePosition;
	
	class colorMap {
	private:
		std::vector<std::pair<color, float>> steps;
	public:
		colorMap(std::vector<std::pair<color, float>> Steps) : steps(Steps) {
			std::sort(steps.begin(), steps.end(), [](std::pair<color, float> lhs, std::pair<color, float> rhs) { return lhs.second < rhs.second; });
		}

		color getColor(float Value) {
			if (Value <= steps.front().second)
				return steps.front().first;

			if (Value >= steps.back().second)
				return steps.back().first;

			auto b = std::upper_bound(steps.begin(), steps.end(), Value, [](float v, std::pair<color, float> p) { return v < p.second; });
			auto a = b - 1;
			float mixA = (b->second - Value) / (b->second - a->second);

			return a->first * mixA + b->first * (1 - mixA);
		}
	};

	colorMap defaultColorMap;

	float getValue(std::complex<double> c) {
		std::complex<double> z = 0;

		for (unsigned i = 0; i < maxIterations; ++i) {
			z = z * z + c;
			if (std::norm(z) >= 4)
				return static_cast<float>(i) / maxIterations;
		}

		return 0;
	}

	std::complex<double> mapComplex(float x, float y) {
		return 2 * Zoom * std::complex<double>(x / width - 0.5, 0.5 - y / height) - translation;
	}

	color generatePixelColor(unsigned x, unsigned y) {
		return defaultColorMap.getColor(getValue(mapComplex(x, y)));
	}

	void threadFunction(unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) {
		for (unsigned iy = y; iy < h + y; ++iy)
			for (unsigned ix = x; ix < w + x; ++ix)
				pixels[ix + iy * width] = generatePixelColor(ix, iy);
	}

	void generateImage(unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) {
		std::vector<std::thread> threads;
		threads.reserve(maxThreads);

		if (w < h) {
			for (unsigned i = 0; i < maxThreads - 1; ++i)
				threads.push_back(std::thread([this](unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) { threadFunction(w, h, x, y); }, w, h / maxThreads, x, y + h / maxThreads * i));
			threads.push_back(std::thread([this](unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) { threadFunction(w, h, x, y); }, w, h - h / maxThreads * (maxThreads - 1), x, y + h / maxThreads * (maxThreads - 1)));
		}
		else {
			for (unsigned i = 0; i < maxThreads - 1; ++i)
				threads.push_back(std::thread([this](unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) { threadFunction(w, h, x, y); }, w / maxThreads, h, x + w / maxThreads * i, y));
			threads.push_back(std::thread([this](unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) { threadFunction(w, h, x, y); }, w - w / maxThreads * (maxThreads - 1), h, x + w / maxThreads * (maxThreads - 1), y));
		}

		for (auto& t : threads)
			if (t.joinable())
				t.join();
		
		texture.update(reinterpret_cast<unsigned char*>(pixels.data()));
	}

	void zoom(double z, sf::Vector2i mouse) {
		auto a = mapComplex(mouse.x, mouse.y);
		Zoom /= z;
		translation += mapComplex(mouse.x, mouse.y) - a;
		generateImage(width, height);
	}

	void reset() {
		Zoom = 2.1;
		translation = { 0, 0 };
		generateImage(width, height);
	}

	void translate(sf::Vector2i t) {
		translation += mapComplex(t.x, -t.y) - mapComplex(0, 0);

		if (t.y > 0)
			for (unsigned row = t.y; row < height; ++row)
				std::memcpy(pixels.data() + (row - t.y) * width + std::max(0, t.x), pixels.data() + row * width - std::min(0, t.x), sizeof(pixel) * (width - std::abs(t.x)));
		else
			for (int row = height + t.y - 1; row >= 0; --row)
				std::memcpy(pixels.data() + (row - t.y) * width + std::max(0, t.x), pixels.data() + row * width - std::min(0, t.x), sizeof(pixel) * (width - std::abs(t.x)));

		generateImage(std::abs(t.x), height, t.x > 0 ? 0 : width + t.x);
		generateImage(width - std::abs(t.x), std::abs(t.y), t.x > 0 ? t.x : 0, t.y > 0 ? height - t.y : 0);
	}

public:
	Application(): defaultColorMap({
		{ {0, 0, 0}, 0 },
		//{ {1, 0.75, 0}, 0.25 },
		//{ {1, 1, 1}, 0.5 },
		//{ {0, 0, 1}, 0.75 },
		{ {1, 1, 1}, 1 }}), pixels(width * height) {
		texture.create(width, height);
		sprite.setTexture(texture);

		reset();

		window.create(sf::VideoMode(width, height), "Complex Sets");
		window.setVerticalSyncEnabled(true);
	}

	bool run() {
		while (window.isOpen()) {
			sf::Event event;
			while (window.pollEvent(event)) {
				switch (event.type) {
				case sf::Event::Closed: window.close(); break;
				case sf::Event::MouseButtonPressed:
					if (event.mouseButton.button == sf::Mouse::Left)
						leftPressed = true;
					break;
				case sf::Event::MouseButtonReleased:
					if (event.mouseButton.button == sf::Mouse::Left)
						leftPressed = false;
					break;
				case sf::Event::MouseMoved:
					if (leftPressed)
						translate({ event.mouseMove.x - lastMousePosition.x, lastMousePosition.y - event.mouseMove.y});
					lastMousePosition = { event.mouseMove.x, event.mouseMove.y };
					break;
				case sf::Event::MouseWheelScrolled:
					zoom(1 + event.mouseWheelScroll.delta / 10.0, lastMousePosition);
					break;
				case sf::Event::KeyReleased:
					switch (event.key.code) {
					case sf::Keyboard::R: reset(); break;
					case sf::Keyboard::Add: maxIterations *= 10; std::cout << "Max Iterations = " << maxIterations << "\n"; generateImage(width, height); break;
					case sf::Keyboard::Subtract: maxIterations /= 10; std::cout << "Max Iterations = " << maxIterations << "\n"; generateImage(width, height); break;
					}
					break;
				}
			}

			window.draw(sprite);
			window.display();
		}

		return 0;
	}
	~Application() = default;
};
