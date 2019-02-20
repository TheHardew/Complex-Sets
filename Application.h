#pragma once
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <chrono>
#include <complex>
#include <iostream>
#include <thread>
#include <vector>

struct color {
	float R, G, B;

	constexpr color operator*(float c) { return { std::min(c * R, 1.f), std::min(c * G, 1.f) , std::min(c * B, 1.f) }; }
	constexpr color operator+(color snd) { return { std::min(R + snd.R, 1.f), std::min(G + snd.G, 1.f), std::min(B + snd.B, 1.f) }; }
};

struct pixel {
	unsigned char R, G, B, A;

	constexpr pixel() : R(0), G(0), B(0), A(0) {}
	constexpr pixel(color c) : R(255 * c.R), G(255 * c.G), B(255 * c.B), A(255) {}
};

template <typename T>
struct vector {
	T x, y;

	constexpr vector() = default;
	constexpr vector(T X, T Y) : x(X), y(Y) {}
	constexpr vector(sf::Vector2<T> v) : x(v.x), y(v.y) {}

	constexpr bool operator==(vector v) { return (x == v.x) && (y == v.y); }
	constexpr bool operator!=(vector v) { return !operator==(v); }

	constexpr vector operator-() { return { -x, -y }; }
	constexpr vector operator+(vector v) { return { x + v.x, y + v.y }; }
	constexpr vector& operator+=(vector v) { operator+(v); return *this; }
	constexpr vector operator-(vector v) { return operator+(-v); }
	constexpr vector& operator-=(vector v) { operator-(v); return *this; }
	template <typename D>
	constexpr vector operator*(D d) { return { static_cast<T>(d * x), static_cast<T>(d * y) }; }
	template <typename D>
	constexpr vector& operator*=(D d) { return *this = operator*(d); }
	template <typename D>
	constexpr vector operator/(D d) { return { static_cast<T>(x / d), static_cast<T>(y / d) }; }
	template <typename D>
	constexpr vector& operator/=(D d) { return *this = operator/(d); }

	template <typename V>
	constexpr operator sf::Vector2<V>() { return { static_cast<V>(x), static_cast<V>(y) }; }
};

using mousePosition = vector<int>;
using resolution = vector<int>;

class Application {
private:
	int width = 1200;
	int height = 800;

	unsigned maxIterations = 100;
	const unsigned maxThreads = std::thread::hardware_concurrency();

	sf::RenderWindow window;

	using numberT = double;
	using complex = std::complex<numberT>;
	complex translation = { 0.75, 0 };
	numberT Zoom = 2.1;

	std::vector<pixel> image;

	sf::Texture texture;
	sf::Sprite sprite;
	
	class colorMap {
	private:
		std::vector<std::pair<color, float>> steps;
	public:
		colorMap(std::vector<std::pair<color, float>> Steps) : steps(Steps) {
			std::sort(steps.begin(), steps.end(), [](std::pair<color, float> lhs, std::pair<color, float> rhs) { return lhs.second < rhs.second; });
		}

		color getColor(float Value) {
			if (Value == 0)
				return { 0, 0, 0 };

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

	const double escapeRadius = 1e2;

	float getValue(const complex c) {
		complex z = 0;

		for (unsigned i = 0; i < maxIterations; ++i) {
			z = z * z + c;
			if (std::norm(z) >= escapeRadius * escapeRadius)
				return (i - std::log2(std::log(std::norm(z)) / 2 / std::log(escapeRadius))) / maxIterations;
		}

		return 0;
	}

	complex screen2complex(double x, double y) {
		return 2 * Zoom* complex((2.0 * x - width) / height / 2, 0.5 - y / height) - translation;
	}

	template <typename T>
	complex screen2complex(vector<T> c) {
		return screen2complex(c.x, c.y);
	}

	vector<double> complex2screen(complex c) {
		c = (c + translation) / 2.0 / Zoom;
		return { (c.real() * 2 * height + width) / 2, -(c.imag() - 0.5) * height };
	}

	color generatePixelColor(vector<double> p) {
		return generatePixelColor(p.x, p.y);
	}

	color generatePixelColor(double x, double y) {
		return defaultColorMap.getColor(getValue(screen2complex(x, y)));
	}

	void generatePixels(unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) {
		for (unsigned iy = y; iy < h + y; ++iy)
			for (unsigned ix = x; ix < w + x; ++ix)
				image[ix + iy * width] = generatePixelColor(ix, iy);
	}

	void generateImage(unsigned w, unsigned h, unsigned x = 0, unsigned y = 0) {
		leftToggled = false;

		auto start = clock::now();

		std::vector<std::thread> threads;
		threads.reserve(maxThreads);

		for (unsigned i = 0; i < maxThreads - 1; ++i)
			threads.push_back(std::thread(&Application::generatePixels, this, w / maxThreads, h, x + w / maxThreads * i, y));
		threads.push_back(std::thread(&Application::generatePixels, this, w - w / maxThreads * (maxThreads - 1), h, x + w / maxThreads * (maxThreads - 1), y));

		for (auto& t : threads)
			if (t.joinable())
				t.join();

		texture.update(reinterpret_cast<unsigned char*>(image.data()));

		auto stop = clock::now();
		std::cout << w * h / std::chrono::duration<double>(clock::now() - start).count() / 1e6 << " Mpx/s\n";
	}

	void zoom(numberT z, mousePosition mouse) {
		auto a = screen2complex(mouse);
		Zoom /= z;
		translation += screen2complex(mouse) - a;

		generateImage(width, height);
		drawFunctionIterations(true);
	}

	void zoom(mousePosition firstVertex, mousePosition secondVertex, bool zoomOut = false) {
		mousePosition upperLeftVertex = { std::min(firstVertex.x, secondVertex.x), std::min(firstVertex.y, secondVertex.y) };
		
		resolution rectangleSize = firstVertex - secondVertex;

		auto yScale = static_cast<double>(std::abs(rectangleSize.y)) / height;
		auto xScale = static_cast<double>(std::abs(rectangleSize.x)) / width;
		auto scale = std::max(yScale, xScale);
		
		auto rectangleCenter = (firstVertex + secondVertex) / 2;
		
		if (zoomOut) {
			Zoom /= scale;
			translation += screen2complex(rectangleCenter) + translation;
		}
		else {
			translation = -screen2complex(rectangleCenter);
			Zoom *= scale;
		}
	
		generateImage(width, height);
		drawFunctionIterations(true);
	}

	void reset() {
		Zoom = 2.1;
		translation = { 0.75, 0 };
		generateImage(width, height);
		drawFunctionIterations(true);
	}

	void translate(mousePosition t) {
		translation += screen2complex(t.x, - t.y) - screen2complex(0, 0);

		if (t.y > 0)
			for (unsigned row = t.y; row < height; ++row)
				std::memcpy(image.data() + (row - t.y) * width + std::max(0, t.x), image.data() + row * width - std::min(0, t.x), sizeof(pixel) * (width - std::abs(t.x)));
		else
			for (int row = height + t.y - 1; row >= 0; --row)
				std::memcpy(image.data() + (row - t.y) * width + std::max(0, t.x), image.data() + row * width - std::min(0, t.x), sizeof(pixel) * (width - std::abs(t.x)));

		generateImage(std::abs(t.x), height, t.x > 0 ? 0 : width + t.x);
		generateImage(width - std::abs(t.x), std::abs(t.y), t.x > 0 ? t.x : 0, t.y > 0 ? height - t.y : 0);
	}

	void setSize(unsigned w, unsigned h, resolution position, bool fullscreen = false) {
		width = w;
		height = h;
		window.create({ w, h }, "Complex Set Viewer", fullscreen ? sf::Style::Fullscreen : sf::Style::Default, sf::ContextSettings(0, 0, 16));

		window.setPosition(position);

		image.resize(width * height);
		texture.create(width, height);
		sprite.setTextureRect({ 0, 0, width, height });

		generateImage(width, height);
	}

	bool fullscreen = false;
	resolution lastPosition;
	resolution lastSize;
	resolution nativeResolution;

	void toggleFullscreen() {
		fullscreen ^= true;
		if (fullscreen) {
			lastPosition = window.getPosition();
			lastSize = { width, height };
			setSize(nativeResolution.x, nativeResolution.y, { 0, 0 }, true);
		}
		else
			setSize(lastSize.x, lastSize.y, lastPosition);
	}

	using clock = std::chrono::high_resolution_clock;
	clock::time_point firstClick = clock::now();

	mousePosition firstRectangleVertex;

	bool leftToggled = false;
	mousePosition lastClickPosition;

	bool middlePressed = false;

	bool rightToggled = false;
	mousePosition lastMousePosition;

	bool shiftPressed = false;

	enum class panningDirection {
		None,
		Up,
		Right,
		Down,
		Left
	} verticalPan, horizontalPan;

	void handleEvent(sf::Event event) {
		switch (event.type) {
		case sf::Event::Closed: 
			window.close(); break;
		case sf::Event::MouseWheelScrolled:
			if (event.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel)
				zoom(1 + event.mouseWheelScroll.delta / 10.0, lastMousePosition); break;
		case sf::Event::Resized: 
			setSize(event.size.width, event.size.height, window.getPosition()); break;
		case sf::Event::MouseButtonPressed:
			if (event.mouseButton.button == sf::Mouse::Left) {
				leftToggled ^= true;

				if (leftToggled)
					firstRectangleVertex = lastMousePosition;
				else if (lastMousePosition == lastClickPosition && clock::now() - firstClick < std::chrono::duration<double>(0.5))
					translate({ width / 2 - event.mouseButton.x, event.mouseButton.y - height / 2 });
				else
					zoom(firstRectangleVertex, lastMousePosition, shiftPressed);

				firstClick = clock::now();
				lastClickPosition = lastMousePosition;
			}
			else if (event.mouseButton.button == sf::Mouse::Middle) 
				middlePressed = true;
			break;
		case sf::Event::MouseButtonReleased:
			switch (event.mouseButton.button) {
			case sf::Mouse::Left: if (lastMousePosition != lastClickPosition) zoom(firstRectangleVertex, lastMousePosition, shiftPressed); break;
			case sf::Mouse::Middle: middlePressed = false; break;
			case sf::Mouse::Right: rightToggled ^= true; break;
			} 
			break;
		case sf::Event::MouseMoved:
			if (middlePressed)
				translate({ event.mouseMove.x - lastMousePosition.x, lastMousePosition.y - event.mouseMove.y });

			lastMousePosition = { event.mouseMove.x, event.mouseMove.y };
			break;
		case sf::Event::KeyPressed:
			switch (event.key.code) {
			case sf::Keyboard::LShift:
			case sf::Keyboard::RShift: shiftPressed = true; break;
			case sf::Keyboard::W:
			case sf::Keyboard::Up: verticalPan = panningDirection::Up; break;
			case sf::Keyboard::D:
			case sf::Keyboard::Right: horizontalPan = panningDirection::Right; break;
			case sf::Keyboard::S:
			case sf::Keyboard::Down: verticalPan = panningDirection::Down; break;
			case sf::Keyboard::A:
			case sf::Keyboard::Left: horizontalPan = panningDirection::Left; break;
			}
			break;
		case sf::Event::KeyReleased:
			switch (event.key.code) {
			case sf::Keyboard::W:
			case sf::Keyboard::Up: if (verticalPan == panningDirection::Up) verticalPan = panningDirection::None; break;
			case sf::Keyboard::D:
			case sf::Keyboard::Right: if (horizontalPan == panningDirection::Right) horizontalPan = panningDirection::None; break;
			case sf::Keyboard::S:
			case sf::Keyboard::Down: if (verticalPan == panningDirection::Down) verticalPan = panningDirection::None; break;
			case sf::Keyboard::A:
			case sf::Keyboard::Left: if (horizontalPan == panningDirection::Left) horizontalPan = panningDirection::None; break;
			case sf::Keyboard::LShift:
			case sf::Keyboard::RShift: shiftPressed = false; break;
			case sf::Keyboard::Escape: leftToggled = false; break;
			case sf::Keyboard::R: reset(); break;
			case sf::Keyboard::Enter: if (!event.key.alt) break;
			case sf::Keyboard::F11: toggleFullscreen(); break;
			case sf::Keyboard::Add: maxIterations *= 10; std::cout << "Max Iterations = " << maxIterations << "\n"; generateImage(width, height); drawFunctionIterations(true); break;
			case sf::Keyboard::Subtract: maxIterations /= 10; std::cout << "Max Iterations = " << maxIterations << "\n"; generateImage(width, height); drawFunctionIterations(true);  break;
			}
			break;
		}
	}

	mousePosition lastGeneratedPosition = { -1, -1 };
	std::vector<sf::Vertex> vertices;

	const sf::RenderStates exclusion = sf::RenderStates(sf::BlendMode(sf::BlendMode::OneMinusDstColor, sf::BlendMode::OneMinusSrcColor, sf::BlendMode::Add));

	void drawFunctionIterations(bool regenerate = false) {	
		if (lastGeneratedPosition != lastMousePosition || regenerate) {
			vertices.clear();
			vertices.push_back({ lastMousePosition });

			auto c = screen2complex(lastMousePosition.x, lastMousePosition.y);
			complex z = 0;
			for (unsigned i = 0; i < maxIterations; ++i) {
				z = z * z + c;
				auto s = complex2screen(z);
				vertices.push_back({ s });
				if (std::abs(s.x) > 5 * width || std::abs(s.y) > 5 * height)
					break;
			}
		
			lastGeneratedPosition = lastMousePosition;
		}
		
		window.draw(vertices.data(), vertices.size(), sf::LineStrip);
	}

	void drawScalingPreview() {
		auto rectangle = sf::RectangleShape(lastMousePosition - firstRectangleVertex);
		rectangle.setFillColor(sf::Color::Transparent);
		rectangle.setOutlineThickness(2);
		rectangle.setPosition(firstRectangleVertex);
		window.draw(rectangle);
	}

	clock::time_point lastFrameStart = clock::now();
	std::chrono::duration<double> frameTime;

public:
	Application() : defaultColorMap({
		{ {0, 7.0 / 255, 100.0 / 255}, 0 },
		{ {32.0 / 255, 107.0 / 255, 203.0 / 255 }, 0.16 },
		{ {237.0 / 255, 255.0 / 255, 255.0 / 255 }, 0.42 },
		{ {255.0 / 255, 170.0 / 255, 0 }, 0.6425 },
		{ {0, 2.0 / 255, 0 }, 0.8575 }, }) {
		sprite.setTexture(texture);

		nativeResolution = { static_cast<int>(sf::VideoMode::getDesktopMode().width), static_cast<int>(sf::VideoMode::getDesktopMode().height) };
		setSize(width, height, (nativeResolution - resolution(width, height)) / 2);

		window.setVerticalSyncEnabled(true);
	}

	void run() {
		while (window.isOpen()) {
			frameTime = clock::now() - lastFrameStart;
			lastFrameStart = clock::now();

			sf::Event event;
			while (window.pollEvent(event))
				handleEvent(event);

			window.clear();

			mousePosition panVector;
			switch (verticalPan) {
			case panningDirection::Up: panVector.y = height; break;
			case panningDirection::Down: panVector.y = -height; break;
			}

			switch (horizontalPan) {
			case panningDirection::Right: panVector.x = width; break;
			case panningDirection::Left: panVector.x = -width; break;
			}

			panVector *= frameTime.count() / 2;
			if (verticalPan != panningDirection::None || horizontalPan != panningDirection::None)
				translate(panVector);

			if (rightToggled)
				drawFunctionIterations();

			if (leftToggled)
				drawScalingPreview();

			window.draw(sprite, exclusion);

			window.display();
		}
	}
};

