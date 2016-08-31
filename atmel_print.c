/*
 * atmel_print.c
 *
 *  Created on: May 5, 2016
 *      Author: water.zhou
 */
#include "stdarg.h"
#include "sys/types.h"
#include "atmel_drv.h"
#include "nmi_uart.h"

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#define is_digit(c) ((c >= '0') && (c <= '9'))

static void A_PUTC(char c)
{
	nm_uart_send(AT_UART_PORT,(uint8 *)&c, 1);
    nm_uart_flush(AT_UART_PORT);
}

static void cmnos_write_char(char c)
{
    if (c == '\n') {
        A_PUTC('\r');
        A_PUTC('\n');
    } else if (c == '\r') {
    	A_PUTC('\r');
    } else {
      A_PUTC(c);
    }
}

static void (*_putc)(char c) = cmnos_write_char;

static int _cvt(unsigned long val, char *buf, long radix, char *digits)
{
    char temp[80];
    char *cp = temp;
    int length = 0;

    if (val == 0) {
        /* Special case */
        *cp++ = '0';
    } else {
        while (val) {
            *cp++ = digits[val % radix];
            val /= radix;
        }
    }
    while (cp != temp) {
        *buf++ = *--cp;
        length++;
    }
    *buf = '\0';
    return (length);
}

static int cmnos_vprintf(void (*putc)(char c), const char *fmt, va_list ap)
{
    char buf[sizeof(long)*8];
    char c, sign, *cp=buf;
    int left_prec, right_prec, zero_fill, pad, pad_on_right,
        i, islong, islonglong;
    long val = 0;
    int res = 0, length = 0;

    while ((c = *fmt++) != '\0') {
        if (c == '%') {
            c = *fmt++;
            left_prec = right_prec = pad_on_right = islong = islonglong = 0;
            if (c == '-') {
                c = *fmt++;
                pad_on_right++;
            }
            if (c == '0') {
                zero_fill = true;
                c = *fmt++;
            } else {
                zero_fill = false;
            }
            while (is_digit(c)) {
                left_prec = (left_prec * 10) + (c - '0');
                c = *fmt++;
            }
            if (c == '.') {
                c = *fmt++;
                zero_fill++;
                while (is_digit(c)) {
                    right_prec = (right_prec * 10) + (c - '0');
                    c = *fmt++;
                }
            } else {
                right_prec = left_prec;
            }
            sign = '\0';
            if (c == 'l') {
                // 'long' qualifier
                c = *fmt++;
		islong = 1;
                if (c == 'l') {
                    // long long qualifier
                    c = *fmt++;
                    islonglong = 1;
                }
            }
            // Fetch value [numeric descriptors only]
            switch (c) {
            case 'p':
		islong = 1;
            case 'd':
            case 'D':
            case 'x':
            case 'X':
            case 'u':
            case 'U':
            case 'b':
            case 'B':
                if (islonglong) {
                    val = va_arg(ap, long);
	        } else if (islong) {
                    val = (long)va_arg(ap, long);
		} else{
                    val = (long)va_arg(ap, int);
                }
                if ((c == 'd') || (c == 'D')) {
                    if (val < 0) {
                        sign = '-';
                        val = -val;
                    }
                } else {
                    // Mask to unsigned, sized quantity
                    if (islong) {
                        val &= (1ULL << (sizeof(long) * 8)) - 1;
                    } else{
                        val &= (1ULL << (sizeof(int) * 8)) - 1;
                    }
                }
                break;
            default:
                break;
            }
            // Process output
            switch (c) {
            case 'p':  // Pointer
                (*putc)('0');
                (*putc)('x');
                zero_fill = true;
                left_prec = sizeof(unsigned long)*2;
            case 'd':
            case 'D':
            case 'u':
            case 'U':
            case 'x':
            case 'X':
                switch (c) {
                case 'd':
                case 'D':
                case 'u':
                case 'U':
                    length = _cvt(val, buf, 10, "0123456789");
                    break;
                case 'p':
                case 'x':
                    length = _cvt(val, buf, 16, "0123456789abcdef");
                    break;
                case 'X':
                    length = _cvt(val, buf, 16, "0123456789ABCDEF");
                    break;
                }
                cp = buf;
                break;
            case 's':
            case 'S':
                cp = va_arg(ap, char *);
                if (cp == NULL)  {
                    cp = "<null>";
                }
                length = 0;
                while (cp[length] != '\0') length++;
                break;
            case 'c':
            case 'C':
                c = va_arg(ap, int /*char*/);
                (*putc)(c);
                res++;
                continue;
            case 'b':
            case 'B':
                length = left_prec;
                if (left_prec == 0) {
                    if (islonglong)
                        length = sizeof(long)*8;
                    else if (islong)
                        length = sizeof(long)*8;
                    else
                        length = sizeof(int)*8;
                }
                for (i = 0;  i < length-1;  i++) {
                    buf[i] = ((val & ((long)1<<i)) ? '1' : '.');
                }
                cp = buf;
                break;
            case '%':
                (*putc)('%');
                break;
            default:
                (*putc)('%');
                (*putc)(c);
                res += 2;
            }
            pad = left_prec - length;
            if (sign != '\0') {
                pad--;
            }
            if (zero_fill) {
                c = '0';
                if (sign != '\0') {
                    (*putc)(sign);
                    res++;
                    sign = '\0';
                }
            } else {
                c = ' ';
            }
            if (!pad_on_right) {
                while (pad-- > 0) {
                    (*putc)(c);
                    res++;
                }
            }
            if (sign != '\0') {
                (*putc)(sign);
                res++;
            }
            while (length-- > 0) {
                c = *cp++;
                (*putc)(c);
                res++;
            }
            if (pad_on_right) {
                while (pad-- > 0) {
                    (*putc)(' ');
                    res++;
                }
            }
        } else {
            (*putc)(c);
            res++;
        }
    }
    return (res);
}

int serial_printfImp(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = cmnos_vprintf(_putc, fmt, ap);
    va_end(ap);

    return (ret);
}

unsigned char crc_8(unsigned char *ptr, unsigned int len)
{
	unsigned char crc;
	unsigned char i;
     	crc = 0;
        while(len--)
	{
		crc ^= *ptr++;
		for(i = 0;i < 8;i++)
		{
			if(crc & 0x01)
			{
				crc = (crc >> 1) ^ 0x8C;
			}else crc >>= 1;
		}
	}
	return crc;
}

static uint32 inet_addr(char *pcIpAddr)
{
	uint8	tmp;
	uint32	u32IP = 0;
	uint8	au8IP[4];
	uint8 	c;
	uint8	i, j;

	tmp = 0;

	for(i = 0; i < 4; ++i)
	{
		j = 0;
		do
		{
			c = *pcIpAddr;
			++j;
			if(j > 4)
			{
				return 0;
			}
			if(c == '.' || c == 0)
			{
				au8IP[i] = tmp;
				tmp = 0;
			}
			else if(c >= '0' && c <= '9')
			{
				tmp = (tmp * 10) + (c - '0');
			}
			else
			{
				if(i == 3)
				{
					au8IP[i] = tmp;
					break;
				}
				return 0;
			}
			++pcIpAddr;
		} while(c != '.' && c != 0);
	}
	memcpy((uint8*)&u32IP, au8IP, 4);
	return u32IP;
}

char *reverse(char *s)
{
    char temp;
    char *p = s;
    char *q = s;
    while(*q)
        ++q;
    q--;

    while(q > p)
    {
        temp = *p;
        *p++ = *q;
        *q-- = temp;
    }
    return s;
}

char *nm_itoa(int n)
{
    int i = 0,isNegative = 0;
    static char s[100];
    if((isNegative = n) < 0)
    {
        n = -n;
    }
    do
    {
        s[i++] = n%10 + '0';
        n = n/10;
    }while(n > 0);

    if(isNegative < 0)
    {
        s[i++] = '-';
    }
    s[i] = '\0';
    return reverse(s);
}

int nm_atoi(const char* str)
{
    int sign = 0,num = 0;
    //if(NULL == str);

    while (*str == ' ')
    {
        str++;
    }
    if ('-' == *str)
    {
        sign = 1;
		str++;
    }
    while ((*str >= '0') && (*str <= '9'))
    {
        num = num*10 + (*str - '0'); //就是这一行，将对应字符转化为数字

        str++;
    }
    if(sign == 1)
        return -num;
    else
        return num;
}
