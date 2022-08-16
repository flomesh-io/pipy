declare namespace crypto {

  /**
   * Public key in asymmetric cryptography.
   */
  declare class PublicKey {

    /**
     * Creates an instance of _PublicKey_.
     */
    constructor(
      pem: string | Data,
      options?: { aliasType?: 'sm2' }
    );
  }

  /**
   * Private key in asymmetric cryptography.
   */
  declare class PrivateKey {

    /**
     * Creates an instance of _PrivateKey_.
     */
    constructor(
      pem: string | Data,
      options?: { aliasType?: 'sm2' }
    );
  }

  /**
   * Certificate in X.509 standard.
   */
  declare class Certificate {

    /**
     * Creates an instance of _Certificate_.
     */
    constructor(pem: string | Data);
  }

  /**
   * Certificate chain in X.509 standard.
   */
  declare class CertificateChain {

    /**
     * Creates an instance of _CertificateChain_.
     */
    constructor(pem: string | Data);
  }

  /**
   * Encryption operation.
   */
  declare class Cipher {

    /**
     * Creates an instance of _Cipher_.
     */
    constructor(
      algorithm: string,
      options: { key: string | Data, iv?: string | Data }
    );

    /**
     * Encrypts data.
     */
    update(data: string | Data): Data;

    /**
     * Encrypts the remained data.
     */
    final(): Data;
  }

  /**
   * Decryption operation.
   */
   declare class Decipher {

    /**
     * Creates an instance of _Decipher_.
     */
    constructor(
      algorithm: string,
      options: { key: string | Data, iv?: string | Data }
    );

    /**
     * Decrypts data.
     */
    update(data: string | Data): Data;

    /**
     * Decrypts the remained data.
     */
    final(): Data;
  }

  /**
   * Digest calculation.
   */
  declare class Hash {

    /**
     * Creates an instance of _Hash_.
     */
    constructor(algorithm: string);

    /**
     * Calculates digest.
     */
    update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url' = 'utf8'): void;

    /**
     * Retrieves the final digest value.
     */
    digest(encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data | string;
  }

  /**
   * HMAC (Hash-based Message Authentication Code) calculation.
   */
  declare class Hmac {

    /**
     * Creates an instance of _Hmac_.
     */
    constructor(algorithm: string, key: string | Data);

    /**
     * Calculates HMAC.
     */
    update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url' = 'utf8'): void;

    /**
     * Retrieves the final HMAC value.
     */
    digest(encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data | string;
  }

  /**
   * Signing operation.
   */
  declare class Sign {

    /**
     * Creates an instance of _Sign_.
     */
    constructor(algorithm: string);

    /**
     * Calculates digest.
     */
    update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url' = 'utf8'): void;

    /**
     * Calculates signature.
     */
    sign(key: crypto.PrivateKey, options?: { id?: Data }): Data;
    sign(key: crypto.PrivateKey, encoding: 'utf8' | 'hex' | 'base64' | 'base64url', options?: { id?: Data }): string;
  }

  /**
   * Signature verification.
   */
  declare class Verify {

    /**
     * Creates an instance of _Verify_.
     */
    constructor(algorithm: string);

    /**
     * Calculates digest.
     */
    update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url' = 'utf8'): void;

    /**
     * Verifies signature.
     */
    verify(key: crypto.PublicKey, signature: Data, options?: { id?: Data }): boolean;
    verify(key: crypto.PublicKey, signature: string, encoding: 'utf8' | 'hex' | 'base64' | 'base64url', options?: { id?: Data }): boolean;
  }

  /**
   * JSON Web Key.
   */
  declare class JWK {

    /**
     * Creates an instance of _JWK_.
     */
    constructor(json: object);
  }

  /**
   * JSON Web Token.
   */
  declare class JWT {

    /**
     * Creates an instace of _JWT_.
     */
    constructor(token: string);

    /**
     * Whether the token is valid.
     */
    readonly isValid: boolean;

    /**
     * Header part of the token.
     */
    readonly header: object;

    /**
     * Payload part of the token.
     */
    readonly payload: object;

    /**
     * Verifies the token.
     */
    verify(key: crypto.JWK | crypto.PublicKey | Data | string): boolean;
  }
}
