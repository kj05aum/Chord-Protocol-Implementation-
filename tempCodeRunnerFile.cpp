#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

using namespace std;

// Function to find the prime factorization of a number
// Returns a map where key is the prime and value is its exponent.
map<long long, int> prime_factorize(long long n) {
    map<long long, int> factors;
    
    // Handle factor 2
    while (n % 2 == 0) {
        factors[2]++;
        n /= 2;
    }
    
    // Handle odd factors
    for (long long i = 3; i * i <= n; i += 2) {
        while (n % i == 0) {
            factors[i]++;
            n /= i;
        }
    }
    
    // Handle the remaining factor (if any)
    if (n > 1) {
        factors[n]++;
    }
    
    return factors;
}

// Function to count the exponent of a prime factor 'p' in a number 'a'
long long count_prime_factor(long long a, long long p) {
    if (p == 0) return 0;
    long long count = 0;
    while (a > 0 && a % p == 0) {
        count++;
        a /= p;
    }
    return count;
}

void solve() {
    // N: number of students (1 <= N <= 10^5) -> use int
    // K: the divisor (2 <= K <= 10^9) -> use long long for safety in prime factorization
    int N;
    long long K;

    // Reading N and K from the first line of standard input
    if (!(cin >> N >> K)) return;

    // A is the array of student numbers (A_i <= 10^9) -> use long long
    vector<long long> A(N);
    for (int i = 0; i < N; ++i) {
        if (!(cin >> A[i])) return;
    }

    // 1. Prime Factorize K
    map<long long, int> k_factors = prime_factorize(K);

    if (k_factors.empty()) {
        // This case shouldn't happen based on constraints (K >= 2), 
        // but if K was 1, the answer would be infinite.
        cout << 0 << endl; 
        return;
    }

    // This variable will hold the minimum ratio, which is our final answer X.
    long long min_x = -1;

    // 2. Count Factors in the entire product P
    for (const auto& pair : k_factors) {
        long long p_i = pair.first;  // The prime factor
        int e_i = pair.second;       // The exponent of p_i in K
        
        long long total_count_p_i = 0; // Total count of p_i in the product P

        // Sum the counts of p_i across all student numbers A_j
        for (long long a_j : A) {
            total_count_p_i += count_prime_factor(a_j, p_i);
        }

        // 3. Determine the maximum X for this prime factor: X = floor(Total_Count / Exponent_in_K)
        long long current_x = total_count_p_i / e_i;

        // Update the minimum X across all prime factors of K
        if (min_x == -1 || current_x < min_x) {
            min_x = current_x;
        }
    }

    // Output the answer
    cout << min_x << endl;
}

int main() {
    // Optimization for faster input/output operations
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    solve();

    return 0;
}