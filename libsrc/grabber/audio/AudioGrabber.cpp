#include <grabber/audio/AudioGrabber.h>
#include <math.h>
#include <QImage>
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <fftw3.h>

// Constants
namespace {
	const uint16_t RESOLUTION = 255;

	//Constants vuMeter
	const QJsonArray DEFAULT_HOTCOLOR { 255,0,0 };
	const QJsonArray DEFAULT_WARNCOLOR { 255,255,0 };
	const QJsonArray DEFAULT_SAFECOLOR { 0,255,0 };
	const int DEFAULT_WARNVALUE { 80 };
	const int DEFAULT_SAFEVALUE { 45 };
	const int DEFAULT_MULTIPLIER { 0 };
	const int DEFAULT_TOLERANCE { 20 };
	const int DEFAULT_FFT_RESOLUTION { 50 };
}

#if (QT_VERSION < QT_VERSION_CHECK(5, 14, 0))
namespace QColorConstants
{
	const QColor Black  = QColor(0xFF, 0x00, 0x00);
	const QColor Red    = QColor(0xFF, 0x00, 0x00);
	const QColor Green  = QColor(0x00, 0xFF, 0x00);
	const QColor Blue   = QColor(0x00, 0x00, 0xFF);
	const QColor Yellow = QColor(0xFF, 0xFF, 0x00);
}
#endif
 //End of constants

AudioGrabber::AudioGrabber()
	: Grabber("AudioGrabber")
	, _deviceProperties()
	, _device("none")
	, _hotColor(QColorConstants::Red)
	, _warnValue(DEFAULT_WARNVALUE)
	, _warnColor(QColorConstants::Yellow)
	, _safeValue(DEFAULT_SAFEVALUE)
	, _safeColor(QColorConstants::Green)
	, _multiplier(DEFAULT_MULTIPLIER)
	, _tolerance(DEFAULT_TOLERANCE)
	, _fft_resolution(DEFAULT_FFT_RESOLUTION)
	, _dynamicMultiplier(INT16_MAX)
	, _started(false)
{
	_fftw_in = (fftw_complex*)malloc(sizeof(fftw_complex) * MAX_FRAMES_PER_BUFFER);
    _fftw_out = (fftw_complex*)malloc(sizeof(fftw_complex) * MAX_FRAMES_PER_BUFFER);

	_fftw_plan = fftw_plan_dft_1d(MAX_FRAMES_PER_BUFFER, _fftw_in, _fftw_out,
								  FFTW_FORWARD, FFTW_ESTIMATE);
}

AudioGrabber::~AudioGrabber()
{
	freeResources();
}

void AudioGrabber::freeResources()
{
	fftw_destroy_plan(_fftw_plan);
    fftw_free(_fftw_in);
    fftw_free(_fftw_out);
}

void AudioGrabber::setDevice(const QString& device)
{
	_device = device;

	if (_started)
	{
		this->stop();
		this->start();
	}
}

void AudioGrabber::setConfiguration(const QJsonObject& config)
{
	QString audioEffect = config["audioEffect"].toString();
	QJsonObject audioEffectConfig = config[audioEffect].toObject();
	_audioEffect = audioEffect;

	if (audioEffect == "vuMeter")
	{
		QJsonArray hotColorArray = audioEffectConfig.value("hotColor").toArray(DEFAULT_HOTCOLOR);
		QJsonArray warnColorArray = audioEffectConfig.value("warnColor").toArray(DEFAULT_WARNCOLOR);
		QJsonArray safeColorArray = audioEffectConfig.value("safeColor").toArray(DEFAULT_SAFECOLOR);

		_hotColor = QColor(hotColorArray.at(0).toInt(), hotColorArray.at(1).toInt(), hotColorArray.at(2).toInt());
		_warnColor = QColor(warnColorArray.at(0).toInt(), warnColorArray.at(1).toInt(), warnColorArray.at(2).toInt());
		_safeColor = QColor(safeColorArray.at(0).toInt(), safeColorArray.at(1).toInt(), safeColorArray.at(2).toInt());
		_warnValue = audioEffectConfig["warnValue"].toInt(DEFAULT_WARNVALUE);
		_safeValue = audioEffectConfig["safeValue"].toInt(DEFAULT_SAFEVALUE);
		_multiplier = audioEffectConfig["multiplier"].toDouble(DEFAULT_MULTIPLIER);
		_tolerance = audioEffectConfig["tolerance"].toInt(DEFAULT_MULTIPLIER);
	}
	else if (audioEffect == "fft")
	{
		QJsonArray hotColorArray = audioEffectConfig.value("hotColor").toArray(DEFAULT_HOTCOLOR);
		QJsonArray warnColorArray = audioEffectConfig.value("warnColor").toArray(DEFAULT_WARNCOLOR);
		QJsonArray safeColorArray = audioEffectConfig.value("safeColor").toArray(DEFAULT_SAFECOLOR);

		_multiplier = audioEffectConfig["multiplier"].toDouble(DEFAULT_MULTIPLIER);
		_tolerance = audioEffectConfig["tolerance"].toInt(DEFAULT_MULTIPLIER);
		_fft_resolution = audioEffectConfig["resolution"].toInt(DEFAULT_FFT_RESOLUTION);
	}
	else
	{
		Error(_log, "Unknown Audio-Effect: \"%s\" configured", QSTRING_CSTR(audioEffect));
	}
}

void AudioGrabber::resetMultiplier()
{
	_dynamicMultiplier = INT16_MAX;
}

void AudioGrabber::processAudioFrame(int16_t* buffer, int length)
{
	// Apply Visualizer and Construct Image

	// TODO: Pass Audio Frame to python and let the script calculate the image.

	// TODO: Support Stereo capture with different meters per side

	// Default VUMeter - Later Make this pluggable for different audio effects

	if (_audioEffect == "vuMeter")
	{
		double averageAmplitude = 0;
		// Calculate the the average amplitude value in the buffer
		for (int i = 0; i < length; i++)
		{
			averageAmplitude += fabs(buffer[i]) / length;
		}

		double * currentMultiplier;

		if (_multiplier < std::numeric_limits<double>::epsilon())
		{
			// Dynamically calculate multiplier.
			const double pendingMultiplier = INT16_MAX / fmax(1.0, averageAmplitude + ((_tolerance / 100.0) * averageAmplitude));

			if (pendingMultiplier < _dynamicMultiplier)
				_dynamicMultiplier = pendingMultiplier;

			currentMultiplier = &_dynamicMultiplier;
		}
		else
		{
			// User defined multiplier
			currentMultiplier = &_multiplier;
		}

		// Apply multiplier to average amplitude
		const double result = averageAmplitude * (*currentMultiplier);

		// Calculate the average percentage
		const double percentage = fmin(result / INT16_MAX, 1);

		// Calculate the value
		const int value = static_cast<int>(ceil(percentage * RESOLUTION));

		// Draw Image
		QImage image(1, RESOLUTION, QImage::Format_RGB888);

		image.fill(QColorConstants::Black);

		int safePixelValue = static_cast<int>(round(( _safeValue / 100.0) * RESOLUTION));
		int warnPixelValue = static_cast<int>(round(( _warnValue / 100.0) * RESOLUTION));

		for (int i = 0; i < RESOLUTION; i++)
		{
			QColor color = QColorConstants::Black;
			int position = RESOLUTION - i;

			if (position < safePixelValue)
			{
				color = _safeColor;
			}
			else if (position < warnPixelValue)
			{
				color = _warnColor;
			}
			else
			{
				color = _hotColor;
			}

			if (position < value)
			{
				image.setPixelColor(0, i, color);
			}
			else
			{
				image.setPixelColor(0, i, QColorConstants::Black);
			}
		}

		// Convert to Image<ColorRGB>
		Image<ColorRgb> finalImage (image.width(),image.height());
		for (int y = 0; y < image.height(); y++)
		{
			memcpy((unsigned char*)finalImage.memptr() + y * image.width() * 3, static_cast<unsigned char*>(image.scanLine(y)), image.width() * 3);
		}

		emit newFrame(finalImage);
	}
	else if (_audioEffect == "fft")
	{
		// Copy audio sample to FFTW's input buffer
		for (unsigned long i = 0; i < length; i++)
		{
			_fftw_in[i][0] = buffer[i];
			_fftw_in[i][1] = 0;
		}
		fftw_execute(_fftw_plan);

		// Compute average for all frequencies
		double averageAmplitude = 0;
		for (int i = 0; i < length/2; i++)
		{
			averageAmplitude += sqrt(_fftw_out[i][0] * _fftw_out[i][0] + _fftw_out[i][1] + _fftw_out[i][1]) / (length/2);
		}

		double * currentMultiplier;

		// if (_multiplier < std::numeric_limits<double>::epsilon())
		if (_multiplier < 1)
		{
			// Dynamically calculate multiplier.
			const double pendingMultiplier = INT16_MAX / fmax(1.0, averageAmplitude + ((_tolerance / 100.0) * averageAmplitude));

			if (pendingMultiplier < _dynamicMultiplier)
				_dynamicMultiplier = pendingMultiplier;

			currentMultiplier = &_dynamicMultiplier;
		}
		else
		{
			// User defined multiplier
			currentMultiplier = &_multiplier;
		}

		// Draw Image
		int image_height = RESOLUTION;
		int image_width = fmin(length/2, _fft_resolution);
		QImage image(image_width, image_height, QImage::Format_RGB888);
		image.fill(QColorConstants::Black);

		int num_bins = length/2;
		int bins_per_pixel = num_bins / image_width;

		int safePixelValue = static_cast<int>(round(( _safeValue / 100.0) * image_height));
		int warnPixelValue = static_cast<int>(round(( _warnValue / 100.0) * image_height));

		for (int i = 0; i < image_width; i++)
		{
			QColor color = QColorConstants::Black;
			double sum_magnitude = 0;
			double avg_magnitude = 0;
			int count = 0;

			// Average the FFT values for each bin.
			for (int j = (i * bins_per_pixel); j < (i + 1) * bins_per_pixel && j < num_bins; j++) {
				double magnitude = sqrt(_fftw_out[j][0] * _fftw_out[j][0] + _fftw_out[j][1] * _fftw_out[j][1]);
				sum_magnitude += magnitude;
				count++;
			}
			avg_magnitude = sum_magnitude / count;

			const double result = avg_magnitude * (*currentMultiplier);
			const double percentage = fmin(result / INT16_MAX, 1);
			const int value = static_cast<int>(ceil(percentage * image_height));

			for (int h = 0; h < image_height; h++)
			{
				int position = RESOLUTION - h;

				if (position < value)
				{
					color.setHsvF((double)i / image_width, 1.0, (double)value / image_height);
					image.setPixelColor(i, h, color);
				}
				else
				{
					image.setPixelColor(i, h, QColorConstants::Black);
				}
			}
		}

		// Convert to Image<ColorRGB>
		Image<ColorRgb> finalImage (image.width(),image.height());
		for (int y = 0; y < image.height(); y++)
		{
			memcpy((unsigned char*)finalImage.memptr() + y * image.width() * 3, static_cast<unsigned char*>(image.scanLine(y)), image.width() * 3);
		}

		emit newFrame(finalImage);
	}
}

Logger* AudioGrabber::getLog()
{
	return _log;
}

bool AudioGrabber::start()
{
	resetMultiplier();

	_started = true;

	return true;
}

void AudioGrabber::stop()
{
	_started = false;
}

void AudioGrabber::restart()
{
	stop();
	start();
}

QJsonArray AudioGrabber::discover(const QJsonObject& /*params*/)
{
	QJsonArray result; // Return empty result
	return result;
}
