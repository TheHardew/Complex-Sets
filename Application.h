#pragma once
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <complex>
#include <iostream>
#include <thread>
#include <vector>

class Application {
private:
	int width = 1200;
	int height = 800;

	sf::Vector2i nativeResolution;

	unsigned maxIterations = 100;

	const unsigned maxThreads = std::thread::hardware_concurrency();

	sf::RenderWindow window;

	std::complex<double> translation = { 0.75, 0 };

	double Zoom = 2.1;

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
	bool rightPressed = false;
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

	std::complex<double> screen2complex(float x, float y) {
		return 2 * Zoom * std::complex<double>((2.0 * x - width) / height / 2, 0.5 - y / height) - translation;
	}

	sf::Vector2<double> complex2screen(std::complex<double> c) {
		c = (c + translation) / 2.0 / Zoom;
		return { (c.real() * 2 * height + width) / 2, -(c.imag() - 0.5) * height };
	}

	color generatePixelColor(unsigned x, unsigned y) {
		return defaultColorMap.getColor(getValue(screen2complex(x, y)));
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
		window.draw(sprite);
	}

	void zoom(double z, sf::Vector2i mouse) {
		auto a = screen2complex(mouse.x, mouse.y);
		Zoom /= z;
		translation += screen2complex(mouse.x, mouse.y) - a;
		generateImage(width, height);
	}

	void reset() {
		Zoom = 2.1;
		translation = { 0.75, 0 };
		generateImage(width, height);
	}

	void translate(sf::Vector2i t) {
		translation += screen2complex(t.x, -t.y) - screen2complex(0, 0);

		if (t.y > 0)
			for (unsigned row = t.y; row < height; ++row)
				std::memcpy(pixels.data() + (row - t.y) * width + std::max(0, t.x), pixels.data() + row * width - std::min(0, t.x), sizeof(pixel) * (width - std::abs(t.x)));
		else
			for (int row = height + t.y - 1; row >= 0; --row)
				std::memcpy(pixels.data() + (row - t.y) * width + std::max(0, t.x), pixels.data() + row * width - std::min(0, t.x), sizeof(pixel) * (width - std::abs(t.x)));

		generateImage(std::abs(t.x), height, t.x > 0 ? 0 : width + t.x);
		generateImage(width - std::abs(t.x), std::abs(t.y), t.x > 0 ? t.x : 0, t.y > 0 ? height - t.y : 0);
	}

	void setSize(unsigned w, unsigned h, sf::Vector2i position, bool fullscreen = false) {
		width = w;
		height = h;
		window.create({ w, h }, "Complex Sets Viewer", fullscreen ? sf::Style::Fullscreen : sf::Style::Default, sf::ContextSettings(0, 0, 16));

		window.setPosition(position);

		pixels.resize(width * height);
		texture.create(width, height);
		sprite.setTextureRect(sf::IntRect(0, 0, w, h));

		generateImage(width, height);
	}

	bool fullscreen = false;
	sf::Vector2i lastPosition;
	sf::Vector2i lastSize;

	void toggleFullscreen() {
		fullscreen = !fullscreen;
		if (fullscreen) {
			lastPosition = window.getPosition();
			lastSize = { width, height };
			setSize(nativeResolution.x, nativeResolution.y, { 0, 0 }, true);
		}
		else
			setSize(lastSize.x, lastSize.y, lastPosition);
		
	}

	void handleEvent(sf::Event event) {
		switch (event.type) {
		case sf::Event::Closed: window.close(); break;
		case sf::Event::MouseButtonPressed:
			if (event.mouseButton.button == sf::Mouse::Left)
				leftPressed = true;
			else if (event.mouseButton.button == sf::Mouse::Right) {
				rightPressed = true;
				drawFunctionIterations();
			}
			break;
		case sf::Event::MouseButtonReleased:
			if (event.mouseButton.button == sf::Mouse::Left)
				leftPressed = false;
			else if (event.mouseButton.button == sf::Mouse::Right) {
				rightPressed = false;
				window.clear();
				window.draw(sprite);
			}
			break;
		case sf::Event::MouseMoved:
			if (leftPressed)
				translate({ event.mouseMove.x - lastMousePosition.x, lastMousePosition.y - event.mouseMove.y });
			else if (rightPressed)
				drawFunctionIterations();
			lastMousePosition = { event.mouseMove.x, event.mouseMove.y };
			break;
		case sf::Event::MouseWheelScrolled:
			zoom(1 + event.mouseWheelScroll.delta / 10.0, lastMousePosition); break;
		case sf::Event::Resized:
			setSize(event.size.width, event.size.height, window.getPosition()); break;
		case sf::Event::KeyReleased:
			switch (event.key.code) {
			case sf::Keyboard::R: reset(); break;
			case sf::Keyboard::F11: toggleFullscreen(); break;
			case sf::Keyboard::Add: maxIterations *= 10; std::cout << "Max Iterations = " << maxIterations << "\n"; generateImage(width, height); break;
			case sf::Keyboard::Subtract: maxIterations /= 10; std::cout << "Max Iterations = " << maxIterations << "\n"; generateImage(width, height); break;
			}
			break;
		}
	}

	sf::Vector2i lastGeneratedPosition;
	std::vector<sf::Vertex> points;

	void drawFunctionIterations() {
		window.clear();
		window.draw(sprite);

		if (lastGeneratedPosition != lastMousePosition) {
			points.clear();
			points.push_back({ { static_cast<float>(lastMousePosition.x), static_cast<float>(lastMousePosition.y) } });

			auto c = screen2complex(lastMousePosition.x, lastMousePosition.y);
			std::complex<double> z = 0;
			for (unsigned i = 0; i < maxIterations; ++i) {
				z = z * z + c;
				sf::Vector2<double> s = complex2screen(z);
				points.push_back({ { static_cast<float>(s.x), static_cast<float>(s.y) } });
				points.push_back(points.back());
				if (s.x > width || s.y > height)
					break;
			}

			lastGeneratedPosition = lastMousePosition;
		}
		
		window.draw(points.data(), points.size(), sf::Lines);
	}

public:
	Application(): defaultColorMap({
		{ {0, 0, 0}, 0 },
		//{ {1, 0.75, 0}, 0.25 },
		//{ {1, 1, 1}, 0.5 },
		//{ {0, 0, 1}, 0.75 },
		{ {1, 1, 1}, 1 }}) {
		sprite.setTexture(texture);

		nativeResolution = { static_cast<int>(sf::VideoMode::getDesktopMode().width), static_cast<int>(sf::VideoMode::getDesktopMode().height) };

		setSize(width, height, { (nativeResolution.x - width) / 2, (nativeResolution.y - height) / 2 });

		//window.setVerticalSyncEnabled(true);
		window.setFramerateLimit(60);
	}

	void run() {
		while (window.isOpen()) {
			sf::Event event;
			while (window.pollEvent(event))
				handleEvent(event);

			window.display();
		}
	}
};

