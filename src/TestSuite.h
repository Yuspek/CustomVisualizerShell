#ifndef TEST_SUITE_H
#define TEST_SUITE_H

/**
 * @brief SpecTer projesinin tüm birimlerini (ShellCore, JobManager, Profiler, UIEngine)
 * doğrulayan 32 testlik kapsamlı otomasyon test paketini çalıştırır.
 * @return Tüm testler geçerse 0, en az bir test başarısız olursa 1 döndürür.
 */
int RunAllSpecTerTests();

#endif // TEST_SUITE_H
