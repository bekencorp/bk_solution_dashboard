#include <driver/gpio.h>

bk_err_t bk_gpio_set_output_value(gpio_id_t gpio_id, bool value)
{
	if (value)
		return bk_gpio_set_output_high(gpio_id);
	else
		return bk_gpio_set_output_low(gpio_id);
}
