void reverse(char *str, int len) {
    int i = 0, j = len - 1, temp;
    while (i < j) {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}

int int_to_str(int x, char str[], int d) {
    int i = 0;
    int sign = 0;
    if (x < 0) {
        x = -x;
        sign = 1;
    }
    while (x) {
        str[i++] = (x % 10) + '0';
        x = x / 10;
    }
    if (i == 0)
        str[i++] = '0';
    if (sign)
        str[i++] = '-';

    reverse(str, i);
    str[i] = '\0';
    return i;
}

void time_to_str(int x, char *buf) {
    if (x < 10) {
        buf[0] = '0';
        int_to_str(x, buf + 1, 0);
    } else {
        int_to_str(x, buf, 0);
    }
}

void ftoa(double n, char *res, int afterpoint) {
    if (n < 0) {
        *res++ = '-';
        n = -n;
    }
    int ipart = (int)n;
    double fpart = n - (double)ipart;
    int i = int_to_str(ipart, res, 0);

    if (afterpoint != 0) {
        res[i] = '.';
        double magic = 1;
        for (int k = 0; k < afterpoint; k++)
            magic *= 10;
        fpart = fpart * magic;
        int_to_str((int)fpart, res + i + 1, afterpoint);
    }
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strlen(const char *str) {
    int len = 0;
    while (str[len])
        len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *temp = dest;
    while ((*dest++ = *src++))
        ;
    return temp;
}

void bzero(void *dest, int len) {
    char *d = (char *)dest;
    while (len--)
        *d++ = 0;
}